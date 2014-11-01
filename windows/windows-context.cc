// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include <amio.h>
#include "windows-base-poller.h"
#include "windows-context.h"
#include "windows-transport.h"
#include "windows-util.h"

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
   request_(RequestType::None),
   count_(0)
{
  memset(&ov_, 0, sizeof(ov_));
}

WinContext::WinContext(Ref<IUserData> data, uintptr_t value)
 : data_(data),
   value_(value),
   request_(RequestType::None),
   count_(0)
{
  memset(&ov_, 0, sizeof(ov_));
}

void
WinContext::attach(RequestType type)
{
  request_ = type;
  if (request_ == RequestType::Message)
    count_++;
}

void
WinContext::detach()
{
  if (request_ == RequestType::Message) {
    if (--count_ != 0)
      return;
  }
  request_ = RequestType::None;
}

BeginOverlappedRequest::BeginOverlappedRequest(Ref<Transport> baseTransport, Ref<IOContext> baseContext, RequestType type)
{
  // Validate the request type.
  switch (type) {
    case RequestType::Read:
    case RequestType::Write:
    case RequestType::Other:
      break;
    default:
      return;
  }

  // Validate the context.
  WinContext *context = baseContext->toWinContext();
  if (context->state() != RequestType::None)
    return;

  // Validate the transport.
  WinTransport *transport = baseTransport->toWinTransport();
  Ref<WinBasePoller> poller = transport->get_poller();
  if (!poller)
    return;

  poller->link(context, transport, type);
  poller_ = poller;
  transport_ = transport;
  context_ = context;
}

OVERLAPPED *
BeginOverlappedRequest::overlapped() const
{
  if (!context_)
    return nullptr;
  return context_->toWinContext()->ov();
}

void
BeginOverlappedRequest::Cancel()
{
  if (!poller_)
    return;

  poller_->toWinBasePoller()->unlink(context_->toWinContext(), transport_->toWinTransport());
}

bool
WinContext::cancel_locked()
{
  if (request_ == RequestType::None || request_ == RequestType::Message)
    return false;

  request_ = RequestType::Cancelled;
  return true;
}
  
