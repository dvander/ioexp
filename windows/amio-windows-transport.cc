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
#include <limits.h>

using namespace amio;
using namespace ke;

WinTransport::WinTransport(HANDLE handle, TransportFlags flags)
 : handle_(handle),
   flags_(flags)
{
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

PassRef<IOError>
WinTransport::Read(Ref<IOContext> baseContext, void *buffer, size_t length)
{
  WinContext *context = baseContext->toWinContext();
  if (!context)
    return eInvalidContext;
  if (context->associated())
    return eContextAlreadyAssociated;
  if (length > INT_MAX)
    return eLengthOutOfRange;

  // AddRef the context before we be potentially it in the port.
  context->AddRef();

  DWORD bytesRead;
  if (ReadFile(handle_, buffer, (DWORD)length, &bytesRead, context->ov())) {
    IOEvent ev(context, nullptr);
    ev.Bytes = bytesRead;
    (void)ev;
    return nullptr;
  }

  DWORD error = GetLastError();
  switch (error) {
   case ERROR_IO_PENDING:
    return nullptr;

   case ERROR_HANDLE_EOF:
   {
     IOEvent ev(context, nullptr);
     ev.Ended = true;
     (void)ev;
     return nullptr;
   }

   default:
    return new WinError(error);
  }
}

PassRef<IOError>
WinTransport::Write(Ref<IOContext> baseContext, const void *buffer, size_t length)
{
  WinContext *context = baseContext->toWinContext();
  if (!context)
    return eInvalidContext;
  if (context->associated())
    return eContextAlreadyAssociated;
  if (length > INT_MAX)
    return eLengthOutOfRange;

  // AddRef the context before we be potentially it in the port.
  context->AddRef();

  DWORD bytesWritten;
  if (WriteFile(handle_, buffer, (DWORD)length, &bytesWritten, context->ov())) {
    IOEvent ev(context, nullptr);
    ev.Bytes = bytesWritten;
    (void)ev;
    return nullptr;
  }

  DWORD error = GetLastError();
  switch (error) {
   case ERROR_IO_PENDING:
    return nullptr;

   default:
    return new WinError(error);
  }
}

PassRef<IOError>
Transport::Read(void *buffer, size_t length, uintptr_t data)
{
  Ref<IOContext> context = IOContext::New(data);
  return Read(context, buffer, length);
}

PassRef<IOError>
Transport::Write(const void *buffer, size_t length, uintptr_t data)
{
  Ref<IOContext> context = IOContext::New(data);
  return Write(context, buffer, length);
}