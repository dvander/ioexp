// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "windows-errors.h"
#include "windows-util.h"
#include "windows-transport.h"
#include "windows-context.h"
#include "windows-iocp.h"

using namespace amio;
using namespace ke;

Ref<GenericError> eTooManyThreads = new GenericError("too many threads trying to call Poll()");

CompletionPort::CompletionPort()
 : port_(NULL),
   concurrent_threads_(0)
{
}

CompletionPort::~CompletionPort()
{
  if (port_)
    CloseHandle(port_);
}

PassRef<IOError>
CompletionPort::Initialize(size_t numConcurrentThreads, size_t nMaxEventsPerPoll)
{
  port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, numConcurrentThreads);
  if (!port_)
    return new WinError();

  if (!buffers_.init(16, nMaxEventsPerPoll))
    return eOutOfMemory;

  concurrent_threads_ = numConcurrentThreads;
  return nullptr;
}

PassRef<IOError>
CompletionPort::attach_unlocked(WinTransport *transport, IOListener *listener)
{
  if (!CreateIoCompletionPort(transport->Handle(), port_, reinterpret_cast<ULONG_PTR>(transport), 0))
    return new WinError();

  transport->attach(this, listener);
  return nullptr;
}

PassRef<IOError>
CompletionPort::post_unlocked(WinContext *context, IOListener *listener)
{
  {
    // Take the lock, since we could race with detach() in Poll().
    AutoMaybeLock lock(lock_);
    WinBasePoller::link(context, listener, RequestType::Message);
  }

  if (!PostQueuedCompletionStatus(port_, 0, reinterpret_cast<ULONG_PTR>(listener), context->ov())) {
    Ref<IOError> error = new WinError();
    AutoMaybeLock lock(lock_);
    WinBasePoller::unlink(context, listener);
    return error;
  }

  return nullptr;
}

PassRef<IOError>
CompletionPort::Poll(int timeoutMs)
{
  if (!gGetQueuedCompletionStatusEx)
    return InternalPollOne(timeoutMs);

  MultiPollBuffer<OVERLAPPED_ENTRY>::Use use(buffers_);
  PollBuffer<OVERLAPPED_ENTRY> *buffer = use.get();
  if (!buffer)
    return eOutOfMemory;

  ULONG nevents;
  if (!gGetQueuedCompletionStatusEx(port_, buffer->get(), buffer->length(), &nevents, timeoutMs, FALSE)) {
    DWORD error = GetLastError();
    if (error == WAIT_TIMEOUT)
      return nullptr;
    return new WinError(error);
  }

  for (size_t i = 0; i < nevents; i++) {
    OVERLAPPED_ENTRY &entry = buffer->at(i);
    WinContext *context = WinContext::fromOverlapped(entry.lpOverlapped);

    Dispatch(context, entry, -1);
  }

  if (nevents == buffer->length())
    buffer->maybeResize();

  return nullptr;
}

PassRef<IOError>
CompletionPort::PollOne(int timeoutMs)
{
  return InternalPollOne(timeoutMs);
}

PassRef<IOError>
CompletionPort::InternalPollOne(int timeoutMs)
{
  OVERLAPPED_ENTRY entry;
  BOOL rv = GetQueuedCompletionStatus(port_, &entry.dwNumberOfBytesTransferred, &entry.lpCompletionKey, &entry.lpOverlapped, timeoutMs);
  if (!rv && !entry.lpOverlapped) {
    DWORD error = GetLastError();
    if (error == WAIT_TIMEOUT)
      return nullptr;
    return new WinError(error);
  }

  // Note: On 32-bit Windows, WSAGetLastError() is just a wrapper around
  // GetLastError().
  DWORD error = 0;
  if (!rv)
    error = GetLastError();

  Dispatch(WinContext::fromOverlapped(entry.lpOverlapped), entry, error);
  return nullptr;
}

// Note that we don't use Ref<> here. There's an extra ref from when we
// initiated the IO event. Even after we call detach(), the context will be 
// held alive by the IOResult.
//
// Note: It's very important that we call WinContext::detach() - until that
// point, we *cannot* early-return!
bool
CompletionPort::Dispatch(WinContext *context, OVERLAPPED_ENTRY &entry, DWORD error)
{
  Ref<IOListener> listener;

  IOResult result;
  result.bytes = entry.dwNumberOfBytesTransferred;
  result.completed = true;

  // We take the lock at this point - since we could race with changeListener
  // or WinContext::cancel().
  RequestType request;
  {
    AutoMaybeLock lock(lock_);

    request = context->state();
    switch (request) {
      case RequestType::Cancelled:
      case RequestType::Read:
      case RequestType::Write:
      case RequestType::Other: {
        Ref<WinTransport> transport = AlreadyRefed<WinTransport>(reinterpret_cast<WinTransport *>(entry.lpCompletionKey));

        // If we have a transport and it's closed - or the IO operation was
        // cancelled - just leave now. Note we always grab the transport even
        // if the state was Cancelled, since we have to acquire its ref.
        if (transport->Closed() || request == RequestType::Cancelled)
          return false;

        // Would be much nicer if we could use RtlNtStatusToDosError() here, but
        // the Internal bits on OVERLAPPED are documented as, well, internal.
        if (error == -1)
          error = transport->GetOverlappedError(entry.lpOverlapped);

        listener = transport->listener();
        break;
      }
      case RequestType::Message:
        listener = AlreadyRefed<IOListener>(reinterpret_cast<IOListener *>(entry.lpCompletionKey));
        break;
      default:
        assert(false);
        break;
    }

    // Detach the context. After this point, we can return whenever we want.
    // Note that the object may have exactly one ref at this point, so it's
    // important that it goes right into a Refed location.
    result.context = WinBasePoller::take(context);
  }

  if (error) {
    if (error == ERROR_HANDLE_EOF)
      result.ended = true;
    else
      result.error = new WinError(error);
  }

  switch (request) {
    case RequestType::Read:
      listener->OnRead(result);
      break;
    case RequestType::Write:
      listener->OnWrite(result);
      break;
    case RequestType::Other:
    case RequestType::Message:
      listener->OnCompleted(result);
      break;
  }

  return true;
}

void
CompletionPort::WaitAndDiscardPendingEvents()
{
  // We assume no one is adding events when this is called...
  while (pending_events_) {
    DWORD ignoreBytes;
    ULONG_PTR ignoreKey;
    OVERLAPPED *ovp;
    if (!GetQueuedCompletionStatus(port_, &ignoreBytes, &ignoreKey, &ovp, INFINITE)) {
      if (!ovp)
        break;
    }

    WinContext *context = WinContext::fromOverlapped(ovp);
    assert(context->state() != RequestType::None);

    switch (context->state()) {
      case RequestType::Cancelled:
      case RequestType::Read:
      case RequestType::Write:
      case RequestType::Other:
        reinterpret_cast<WinTransport *>(context->ov())->Release();
        break;
      case RequestType::Message:
        reinterpret_cast<IOListener *>(context->ov())->Release();
        break;
    }

    // We already derefed the object above so pass null here.
    WinBasePoller::unlink(context, static_cast<IRefcounted *>(nullptr));
  }
}

bool
CompletionPort::enable_immediate_delivery_locked()
{
  return gSetFileCompletionNotificationModes != nullptr;
}