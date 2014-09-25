// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "posix/amio-posix-errors.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

using namespace amio;

ke::Ref<GenericError> amio::eIncompatibleTransport = new GenericError("transport is not a PosixTransport");
ke::Ref<GenericError> amio::eTransportClosed = new GenericError("transport is closed");

PosixError::PosixError()
 : errcode_(errno),
   computed_message_(false)
{
}

PosixError::PosixError(int errcode)
 : errcode_(errcode),
   computed_message_(false)
{
}

const char *
PosixError::Message()
{
  if (computed_message_)
    return message_;

#if !defined(__linux__) || ((_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && ! _GNU_SOURCE)
  if (strerror_r(errcode_, message_, sizeof(message_)) != 0)
    snprintf(message_, sizeof(message_), "Unknown error %d", errcode_);
#else
  char *message = strerror_r(errcode_, message_, sizeof(message_));
  if (message != message_)
    return message;
#endif

  computed_message_ = true;
  return message_;
}
