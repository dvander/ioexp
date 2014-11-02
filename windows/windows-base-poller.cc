// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
#include "../shared/shared-errors.h"
#include "windows-errors.h"
#include "windows-base-poller.h"
#include "windows-transport.h"
#include "windows-context.h"

using namespace ke;
using namespace amio;

WinBasePoller::WinBasePoller()
 : pending_events_(0),
   immediate_delivery_(false),
   immediate_delivery_required_(false)
{
}

PassRef<IOError>
WinBasePoller::Attach(Ref<Transport> baseTransport, Ref<IOListener> listener)
{
  WinTransport *transport = baseTransport->toWinTransport();
  if (!transport)
    return eIncompatibleTransport;
  if (transport->listener())
    return eTransportAlreadyAttached;
  if (transport->Closed())
    return eTransportClosed;

  {
    AutoMaybeLock lock(lock_);
    if (immediate_delivery_ && !transport->ImmediateDelivery()) {
      Ref<IOError> error = transport->EnableImmediateDelivery();
      if (error && immediate_delivery_required_)
        return error;
    }
  }

  return attach_unlocked(transport, listener);
}

PassRef<IOError>
WinBasePoller::Post(Ref<IOContext> baseContext, Ref<IOListener> listener)
{
  WinContext *context = baseContext->toWinContext();

  switch (context->state()) {
    // Okay to re-use contexts for Post(), since it does not modify the
    // OVERLAPPED structure.
    case RequestType::Message:
    case RequestType::None:
      break;
    default:
      return eInvalidContext;
  }

  return post_unlocked(context, listener);
}

void
WinBasePoller::EnableThreadSafety()
{
  lock_ = new Mutex();
}

bool
WinBasePoller::EnableImmediateDelivery()
{
  AutoMaybeLock lock(lock_);
  if (immediate_delivery_)
    return true;

  if (!enable_immediate_delivery_locked())
    return false;

  immediate_delivery_ = true;
  return true;
}

bool
WinBasePoller::RequireImmediateDelivery()
{
  AutoMaybeLock lock(lock_);
  if (immediate_delivery_)
    return true;

  if (!enable_immediate_delivery_locked())
    return false;

  immediate_delivery_ = true;
  immediate_delivery_required_ = true;
  return true;
}

void
WinBasePoller::link_impl(WinContext *context, RequestType type)
{
  // The kernel now owns the OVERLAPPED pointer in the context, so we add one
  // ref for that.
  context->AddRef();
  context->attach(type);
}

void
WinBasePoller::unlink_impl(WinContext *context)
{
  // Undo everything we did in link().
  context->detach();
  context->Release();
}

AlreadyRefed<WinContext>
WinBasePoller::take(WinContext *context)
{
  context->detach();
  return AlreadyRefed<WinContext>(context);
}
