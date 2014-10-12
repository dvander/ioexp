// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include <amio-windows.h>
#include "amio-windows-base-poller.h"
#include "amio-windows-context.h"
#include "amio-windows-transport.h"
#include "amio-windows-util.h"

using namespace amio;
using namespace ke;

PassRef<IOContext>
IOContext::New(uintptr_t value)
{
  return new WinContext(value);
}

PassRef<IOContext>
IOContext::New(Ref<IUserData> data, uintptr_t value)
{
  return new WinContext(data, value);
}

WinContext::WinContext(uintptr_t value)
 : value_(value),
   request_(RequestType::None)
{
  memset(&ov_, 0, sizeof(ov_));
}

WinContext::WinContext(Ref<IUserData> data, uintptr_t value)
 : data_(data),
   value_(value),
   request_(RequestType::None)
{
  memset(&ov_, 0, sizeof(ov_));
}

void
WinContext::attach(RequestType type, PassRef<WinTransport> transport)
{
  // Add a reference because the kernel now owns the OVERLAPPED*.
  AddRef();
  request_ = type;
  transport_ = transport;
  poller_ = transport_->poller();
  poller_->addPendingEvent();
}

void
WinContext::detach()
{
  poller_->removePendingEvent();
  poller_ = nullptr;
  transport_ = nullptr;
  request_ = RequestType::None;

  // Release the reference we added for the kernel.
  Release();
}

OVERLAPPED *
WinContext::LockForOverlappedIO(Ref<Transport> transport, RequestType request)
{
  WinTransport *wint = transport->toWinTransport();
  if (!wint ||
      request == RequestType::None ||
      request == RequestType::Cancelled ||
      !wint->poller() ||
      request_ != RequestType::None)
  {
    return nullptr;
  }

  attach(request, wint);
  return ov();
}

void
WinContext::UnlockForFailedOverlappedIO()
{
  if (request_ != RequestType::None)
    detach();
}

void
WinContext::Cancel()
{
  if (request_ == RequestType::None)
    return;
  
  // Note that even if we manage to call CancelIoEx(), we'll still get a packet
  // queued to the port. Thus, we don't detach here.
  request_ = RequestType::Cancelled;
  if (gCancelIoEx)
    gCancelIoEx(transport_->Handle(), ov());
}
