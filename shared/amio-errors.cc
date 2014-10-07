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

ke::Ref<GenericError> amio::eTransportAlreadyAttached = new GenericError("transport already attached");
ke::Ref<GenericError> amio::eOutOfMemory = new GenericError("out of memory");
ke::Ref<GenericError> amio::eUnknownHangup = new GenericError("unknown hangup");
ke::Ref<GenericError> amio::eTransportClosed = new GenericError("transport is closed");
ke::Ref<GenericError> amio::eUnsupportedAddressFamily = new GenericError("unsupported address family");
ke::Ref<GenericError> amio::eUnsupportedProtocol = new GenericError("unsupported protocol");

#if !defined(KE_CXX11)
const ErrorType ErrorType::System = {0};
const ErrorType ErrorType::Library = {1};
const ErrorType ErrorType::Exception = {2};
#endif

GenericError::GenericError(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  message_ = FormatStringVa(fmt, ap);
  va_end(ap);
  assert(message_);
}
