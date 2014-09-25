// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "../shared/amio-errors.h"
#include "../shared/amio-string.h"

using namespace amio;

ke::Ref<GenericError> amio::eTransportAlreadyRegistered;
ke::Ref<GenericError> amio::eOutOfMemory;
ke::Ref<GenericError> amio::eUnknownHangup;

class InitializeSharedErrors
{
 public:
  InitializeSharedErrors() {
    eTransportAlreadyRegistered = new GenericError("transport already registered to a message pump");
    eOutOfMemory = new GenericError("out of memory");
    eUnknownHangup = new GenericError("unknown hangup");
  }
} sSharedErrorInitializer;

GenericError::GenericError(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  message_ = FormatStringVa(fmt, ap);
  va_end(ap);
  assert(message_);
}
