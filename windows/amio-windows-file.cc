// vim: set ts=2 sw=2 tw=99 et:
// 
// copyright (c) 2014 david anderson
// 
// this file is part of the alliedmodders i/o library.
// 
// the alliedmodders i/o library is licensed under the gnu general public
// license, version 3 or higher. for more information, see license.txt
//
#include "amio-windows-errors.h"
#include "amio-windows-context.h"
#include "amio-windows-file.h"
#include "amio-windows-util.h"
#include "amio-windows-base-poller.h"

using namespace amio;
using namespace ke;

FileTransport::FileTransport(HANDLE handle, TransportFlags flags)
 : WinTransport(flags),
   handle_(handle)
{
}

FileTransport::~FileTransport()
{
  Close();
}

void
FileTransport::Close()
{
  if (handle_ == INVALID_HANDLE_VALUE || handle_ == NULL)
    return;

  if (!(flags_ & kTransportNoAutoClose))
    CloseHandle(handle_);
  handle_ = NULL;

  WinTransport::Close();
}

PassRef<IOError>
FileTransport::EnableImmediateDelivery()
{
  Ref<IOError> error = ::EnableImmediateDelivery(handle_);
  if (error)
    return error;
  flags_ = TransportFlags(flags_ | kTransportImmediateDelivery);
  return nullptr;
}

bool
FileTransport::Read(IOResult *r, Ref<IOContext> baseContext, void *buffer, size_t length)
{
  WinContext *context = checkOp(r, baseContext, length);
  if (!context)
    return false;

  // We have to attach the context before we potentially send something to a
  // completion port. Otherwise, it could be resolved and dequeued on another
  // thread before we get a chance to increment its reference, and Poll() could
  // prematurely destroy it.
  context->attach(RequestType::Read, this);
  *r = IOResult();

  DWORD bytesRead;
  BOOL rv = ReadFile(handle_, buffer, (DWORD)length, &bytesRead, context->ov());
  DWORD error = rv ? 0 : GetLastError();

  if (!rv &&
      !(error == ERROR_IO_PENDING ||
        error == ERROR_MORE_DATA ||
        error == ERROR_HANDLE_EOF))
  {
    // Some kind of unrecognizable error occurred. Nothing will be posted to
    // the port, hopefully.
    context->detach();
    *r = IOResult(new WinError(error), context);
    return false;
  }

  // If results are pending, do nothing else.
  if (error == ERROR_IO_PENDING)
    return true;

  r->bytes = size_t(bytesRead);
  r->completed = true;

  // The docs are very vague here, so this may take some trial and error. As
  // far as I can tell, MORE_DATA enqueues into an IOCP based on the delivery
  // mode, since it is not a real error. EOF is unclear, and examples online
  // are inconsistent.
  switch (error) {
  case ERROR_HANDLE_EOF:
    r->ended = true;
    break;
  case ERROR_MORE_DATA:
    r->moreData = true;
    break;
  }

  if (ImmediateDelivery()) {
    // No event will be posted to the IOCP, so steal back our reference.
    r->context = context;
    context->detach();
  }

  return true;
}

bool
FileTransport::Write(IOResult *r, Ref<IOContext> baseContext, const void *buffer, size_t length)
{
  WinContext *context = checkOp(r, baseContext, length);
  if (!context)
    return false;

  // See the comment in Read().
  context->attach(RequestType::Read, this);
  *r = IOResult();

  DWORD bytesWritten;
  BOOL rv = WriteFile(handle_, buffer, (DWORD)length, &bytesWritten, context->ov());
  DWORD error = rv ? 0 : GetLastError();

  if (error && error != ERROR_IO_PENDING) {
    // Some kind of unrecognizable error occurred. Nothing will be posted to
    // the port, hopefully.
    context->detach();
    *r = IOResult(new WinError(error), context);
    return false;
  }

  // If results are pending, do nothing else.
  if (error == ERROR_IO_PENDING)
    return true;

  r->bytes = size_t(bytesWritten);
  r->completed = true;

  if (ImmediateDelivery()) {
    // No event will be posted to the IOCP, so steal back our reference.
    r->context = context;
    context->detach();
  }

  return true;
}
