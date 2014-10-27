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
  poller_ = nullptr;
  listener_ = nullptr;
}

WinContext *
WinTransport::checkOp(IOResult *r, Ref<IOContext> baseContext, size_t length)
{
  // Okay to not Ref<> - it's held alive above.
  WinContext *context = baseContext->toWinContext();
  if (!context || Closed()) {
    *r = IOResult(eInvalidContext, baseContext);
    return nullptr;
  }
  if (!poller_) {
    *r = IOResult(eTransportNotAttached, context);
    return nullptr;
  }
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
