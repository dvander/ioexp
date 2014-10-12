// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "amio-windows-errors.h"
#include "amio-windows-util.h"
#include "amio-windows-transport.h"
#include "amio-windows-context.h"
#include "amio-windows-iocp.h"

using namespace amio;
using namespace ke;

Ref<GenericError> eTooManyThreads = new GenericError("too many threads trying to call Poll()");

CompletionPort::CompletionPort()
 : port_(NULL),
   concurrent_threads_(0),
   immediate_delivery_(false),
   immediate_delivery_required_(false)
{
}

CompletionPort::~CompletionPort()
{
  if (port_)
    CloseHandle(port_);
}

PassRef<IOError>
CompletionPort::Initialize(size_t numConcurrentThreads)
{
  port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, numConcurrentThreads);
  if (!port_)
    return new WinError();

  concurrent_threads_ = numConcurrentThreads;
  return nullptr;
}

PassRef<IOError>
CompletionPort::Attach(Ref<Transport> baseTransport, Ref<IOListener> listener)
{
  WinTransport *transport = baseTransport->toWinTransport();
  if (!transport)
    return eIncompatibleTransport;
  if (transport->listener())
    return eTransportAlreadyAttached;
  if (transport->Closed())
    return eTransportClosed;

  if (immediate_delivery_ && !transport->ImmediateDelivery()) {
    Ref<IOError> error = transport->EnableImmediateDelivery();
    if (error && immediate_delivery_required_)
      return error;
  }

  if (!CreateIoCompletionPort(transport->Handle(), port_, 0, 0))
    return new WinError();

  transport->attach(this, listener);
  return nullptr;
}

PassRef<IOError>
CompletionPort::Poll(int timeoutMs)
{
  // We try to get at least one valid event.
  DWORD start = GetTickCount();
  DWORD nextTimeout = timeoutMs;
  while (true) {
    size_t nevents;
    if (Ref<IOError> error = InternalPoll(nextTimeout, &nevents))
      return error;
    if (nevents)
      return nullptr;

    if (timeoutMs == kNoTimeout)
      continue;

    nextTimeout = timeoutMs - (GetTickCount() - start);
    if (nextTimeout <= 0)
      return nullptr;
  }
}

PassRef<IOError>
CompletionPort::InternalPoll(int timeoutMs, size_t *nevents)
{
  *nevents = 0;

  DWORD numBytes;
  ULONG_PTR key;
  OVERLAPPED *ovp;
  BOOL rv = GetQueuedCompletionStatus(port_, &numBytes, &key, &ovp, timeoutMs);
  if (!rv && !ovp) {
    DWORD error = GetLastError();
    if (error == WAIT_TIMEOUT)
      return nullptr;
    return new WinError(error);
  }

  // Note that we don't use Ref<> here. There's an extra ref from when we
  // initiated the IO event. Even after we call detach(), the context wil be
  // held alive by the IOResult.
  WinContext *context = WinContext::fromOverlapped(ovp);
  assert(context->state() != RequestType::None);

  IOResult result;
  result.bytes = numBytes;
  result.completed = true;
  result.context = context;
  if (!rv) {
    DWORD error = context->transport()->LastError();
    if (error == ERROR_HANDLE_EOF)
      result.ended = true;
    else
      result.error = new WinError(error);
  }

  // Detach the context.
  RequestType state = context->state();
  Ref<WinTransport> transport = context->transport();
  context->detach();

  // If the transport is closed, just eat the event. The refcount will drop to
  // zero (hopefully) once we exit the poll operation, which will also free
  // any attached user data.
  if (transport->Closed())
    return nullptr;

  // Notify. Note that we must do this even if the transport has been closed;
  // otherwise, there is no way for the user to free contexts.
  switch (state) {
    case RequestType::Read:
      transport->listener()->OnRead(transport, result);
      break;
    case RequestType::Write:
      transport->listener()->OnWrite(transport, result);
      break;
    case RequestType::Other:
      transport->listener()->OnOther(transport, result);
      break;
    case RequestType::Cancelled:
      break;
  }

  *nevents = 1;
  return nullptr;
}

bool
CompletionPort::EnableImmediateDelivery()
{
  if (immediate_delivery_)
    return true;

  if (!gSetFileCompletionNotificationModes)
    return false;

  immediate_delivery_ = true;
  return true;
}

bool
CompletionPort::RequireImmediateDelivery()
{
  if (!EnableImmediateDelivery())
    return false;
  immediate_delivery_required_ = true;
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

    // Note: we don't call Release() or use Ref<> here. Calling detach() calls
    // Release(), so after we assume it may have been deleted.
    WinContext *context = WinContext::fromOverlapped(ovp);
    assert(context->state() != RequestType::None);
    context->detach();
  }
}
