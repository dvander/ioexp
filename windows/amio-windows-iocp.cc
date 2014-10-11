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
  assert(context->state() != WinContext::None);

  IOResult result;
  result.bytes = numBytes;
  result.completed = true;
  result.context = context;
  if (!rv) {
    DWORD error = GetLastError();
    if (error = ERROR_HANDLE_EOF)
      result.ended = true;
    else
      result.error = new WinError(error);
  }

  // Detach the context.
  WinContext::State state = context->state();
  Ref<WinTransport> transport = context->transport();
  context->detach();

  // Decrement the number of pending events.
  removePendingEvent();

  // If the transport has been closed, don't send any further events.
  if (transport->Closed())
    return nullptr;

  // Notify.
  switch (state) {
  case WinContext::Reading:
    transport->listener()->OnRead(transport, result);
    break;
  case WinContext::Writing:
    transport->listener()->OnWrite(transport, result);
    break;
  }

  return nullptr;
}

bool
CompletionPort::EnableImmediateDelivery()
{
  if (immediate_delivery_)
    return true;

  if (!CanEnableImmediateDelivery())
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
    assert(context->state() != WinContext::None);
    context->detach();
    removePendingEvent();
  }
}
