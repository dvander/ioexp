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
ke::Ref<GenericError> amio::ePollerShutdown = new GenericError("poller has been shutdown");
ke::Ref<GenericError> amio::eTransportNotAttached = new GenericError("transport is not attached");
ke::Ref<GenericError> amio::eEdgeTriggeringUnsupported = new GenericError("native edge-triggering is not supported");

GenericError::GenericError(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  message_ = FormatStringVa(fmt, ap);
  va_end(ap);
  assert(message_);
}
