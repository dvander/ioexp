// vim: set ts=2 sw=2 tw=99 et:
// 
// copyright (c) 2014 david anderson
// 
// this file is part of the alliedmodders i/o library.
// 
// the alliedmodders i/o library is licensed under the gnu general public
// license, version 3 or higher. for more information, see license.txt
//
#include <amio.h>
#include "windows-context.h"
#include "windows-errors.h"
#include "windows-base-poller.h"
#include "windows-transport.h"
#include "windows-util.h"
#include <limits.h>

using namespace amio;
using namespace ke;

WinTransport::WinTransport(TransportFlags flags)
 : flags_(flags)
{
}

void
WinTransport::Close()
{
  // Clear the listener and poller, just to get rid of its ref early.
  if (Ref<WinBasePoller> poller = poller_.get()) {
    // Note: we have to protect the listener when we do this.
    AutoMaybeLock lock(poller->lock());
    poller_ = nullptr;
    listener_ = nullptr;
  }
}

WinContext *
WinTransport::checkOp(IOResult *r, Ref<IOContext> baseContext, size_t length)
{
  // Okay to not Ref<> - it's held alive above.
  if (Closed()) {
    *r = IOResult(eInvalidContext, baseContext);
    return nullptr;
  }

  WinContext *context = baseContext->toWinContext();
  if (context->state() != RequestType::None) {
    *r = IOResult(eContextAlreadyAssociated, context);
    return nullptr;
  }
  if (length > INT_MAX) {
    *r = IOResult(eLengthOutOfRange, context);
    return nullptr;
  }
  return context;
}

bool
WinTransport::Read(IOResult *r, Ref<IOContext> baseContext, void *buffer, size_t length)
{
  WinContext *context = checkOp(r, baseContext, length);
  if (!context)
    return false;

  Ref<WinBasePoller> poller = get_poller();
  if (!poller) {
    *r = IOResult(eTransportNotAttached, context);
    return nullptr;
  }

  return read(r, poller, context, buffer, length);
}

bool
WinTransport::Write(IOResult *r, Ref<IOContext> baseContext, const void *buffer, size_t length)
{
  WinContext *context = checkOp(r, baseContext, length);
  if (!context)
    return false;

  Ref<WinBasePoller> poller = get_poller();
  if (!poller) {
    *r = IOResult(eTransportNotAttached, context);
    return nullptr;
  }

  return write(r, poller, context, buffer, length);
}

void
WinTransport::Cancel(Ref<IOContext> baseContext)
{
  WinContext *context = baseContext->toWinContext();
  Ref<WinBasePoller> poller = get_poller();
  if (!poller)
    return;

  AutoMaybeLock lock(poller->lock());
  if (!context->cancel_locked())
    return;

  // Note that even if we manage to call CancelIoEx(), we'll still get a packet
  // queued to the port. Thus, we don't detach here.
  if (gCancelIoEx)
    gCancelIoEx(Handle(), context->ov());
}

IOResult
Transport::Read(void *buffer, size_t length, uintptr_t data)
{
  Ref<IOContext> context = IOContext::New(data);
  IOResult r;
  Read(&r, context, buffer, length);
  return r;
}

IOResult
Transport::Write(const void *buffer, size_t length, uintptr_t data)
{
  Ref<IOContext> context = IOContext::New(data);
  IOResult r;
  Write(&r, context, buffer, length);
  return r;
}

void
WinTransport::changeListener(Ref<IOListener> listener)
{
  // This is a potentially dangerous operation, since - and this cannot happen
  // in current uses, but it might in the future - we could change the listener
  // during a Poll(), which could cause its refcount to race. Therefore if we
  // have a poller we always lock before changing its listener.
  //
  // This could race with Attach(), of course. We're assuming that won't happen.
  if (Ref<WinBasePoller> poller = poller_.get()) {
    AutoMaybeLock lock(poller->lock());
    listener_ = listener;
  } else {
    listener_ = listener;
  }
}
