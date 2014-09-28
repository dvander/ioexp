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

using namespace amio;
using namespace ke;

PassRef<IOContext>
IOContext::New(uintptr_t data)
{
  return new WinContext(data);
}

WinContext::WinContext(uintptr_t data)
 : data_(data),
   state_(WinContext::None)
{
  memset(&ov_, 0, sizeof(ov_));
}

void
WinContext::attach(State state, PassRef<WinTransport> transport)
{
  AddRef();
  state_ = state;
  transport_ = transport;
  transport_->poller()->addPendingEvent();
}

void
WinContext::detach()
{
  transport_->poller()->removePendingEvent();
  transport_ = nullptr;
  state_ = None;
  Release();
}
