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
#include "amio-windows-errors.h"
#include <stdio.h>

using namespace amio;
using namespace ke;

Ref<GenericError> amio::eContextAlreadyAssociated = new GenericError("context is already in-use");
Ref<GenericError> amio::eTransportNotAttached = new GenericError("transport is not associated with a poller");
Ref<GenericError> amio::eLengthOutOfRange = new GenericError("number of bytes is too large");
Ref<GenericError> amio::eInvalidContext = new GenericError("invalid context");
Ref<GenericError> amio::eIncompatibleTransport = new GenericError("transport is not a WinTransport");
Ref<GenericError> amio::eSocketClosed = new GenericError("socket is closed");
Ref<GenericError> amio::eIncompatibleSocket = new GenericError("socket is not a WinSocket");
Ref<GenericError> amio::eSocketAlreadyAttached = new GenericError("socket is already attached");

WinError::WinError()
 : error_(GetLastError()),
   message_(nullptr)
{
}

WinError::WinError(DWORD error)
 : error_(error),
   message_(nullptr)
{
}

WinError::~WinError()
{
  if (message_)
    LocalFree(message_);
}

const char *
WinError::Message()
{
  if (message_)
    return message_;

  DWORD result = FormatMessageA(
    FORMAT_MESSAGE_ALLOCATE_BUFFER |
      FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS,
    nullptr,
    error_,
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    (LPSTR)&message_,
    0,
    nullptr
  );
  if (result == 0) {
    char buffer[255];
    _snprintf(buffer, sizeof(buffer), "error %d while formatting error %d", GetLastError(), error_);

    message_ = (char *)LocalAlloc(LPTR, strlen(buffer) + 1);
    if (!message_)
      return "out of memory";
    strcpy(message_, buffer);
  }

  return message_;
}
