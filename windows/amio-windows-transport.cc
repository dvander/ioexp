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
#include "amio-windows-context.h"
#include "amio-windows-errors.h"
#include "amio-windows-transport.h"
#include "amio-windows-util.h"
#include <limits.h>

using namespace amio;
using namespace ke;

WinTransport::WinTransport(HANDLE handle, TransportFlags flags)
 : handle_(handle),
   flags_(flags)
{
  if (flags_ & kTransportImmediateDelivery)
    flags_ = TransportFlags(flags & ~kTransportImmediateDelivery);
}

WinTransport::~WinTransport()
{
  Close();
}

void
WinTransport::Close()
{
  if (handle_ == INVALID_HANDLE_VALUE)
    return;

  if (!(flags_ & kTransportNoAutoClose))
    CloseHandle(handle_);
  handle_ = INVALID_HANDLE_VALUE;
}

bool
WinTransport::Read(IOResult *r, Ref<IOContext> baseContext, void *buffer, size_t length)
{
  WinContext *context = baseContext->toWinContext();
  if (!context) {
    *r = IOResult(eInvalidContext, baseContext);
    return false;
  }
  if (context->associated()) {
    *r = IOResult(eContextAlreadyAssociated, context);
    return false;
  }
  if (length > INT_MAX) {
    *r = IOResult(eLengthOutOfRange, context);
    return false;
  }

  // AddRef the context before we be potentially it in the port.
  context->AddRef();
  *r = IOResult();

  DWORD bytesRead;
  if (ReadFile(handle_, buffer, (DWORD)length, &bytesRead, context->ov())) {
    r->Completed = true;
    r->Bytes = size_t(bytesRead);
    if (ImmediateDelivery())
      r->Context = context;
  } else {
    DWORD error = GetLastError();
    switch (error) {
    case ERROR_IO_PENDING:
      break;

    case ERROR_HANDLE_EOF:
      r->Completed = true;
      r->Ended = true;
      r->Bytes = size_t(bytesRead);
      if (ImmediateDelivery())
        r->Context = context;
      break;

    default:
      *r = IOResult(new WinError(error), context);
      return false;
    }
  }

  // If an event was posted, force an extra ref on the context.
  if (!r->Context)
    context->AddRef();
  return true;
}

bool
WinTransport::Write(IOResult *r, Ref<IOContext> baseContext, const void *buffer, size_t length)
{
  WinContext *context = baseContext->toWinContext();
  if (!context) {
    *r = IOResult(eInvalidContext, baseContext);
    return false;
  }
  if (context->associated()) {
    *r = IOResult(eContextAlreadyAssociated, context);
    return false;
  }
  if (length > INT_MAX) {
    *r = IOResult(eLengthOutOfRange, context);
    return false;
  }

  // AddRef the context before we be potentially it in the port.
  context->AddRef();
  *r = IOResult();

  DWORD bytesRead;
  if (WriteFile(handle_, buffer, (DWORD)length, &bytesRead, context->ov())) {
    r->Completed = true;
    r->Bytes = size_t(bytesRead);
    if (ImmediateDelivery())
      r->Context = context;
  } else {
    DWORD error = GetLastError();
    switch (error) {
    case ERROR_IO_PENDING:
      break;

    default:
      *r = IOResult(new WinError(error), context);
      return false;
    }
  }

  // If an event was posted, force an extra ref on the context.
  if (!r->Context)
    context->AddRef();
  return true;
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
