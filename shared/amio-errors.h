// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_shared_error_h_
#define _include_amio_shared_error_h_

#include "../include/amio.h"
#include <stdarg.h>

namespace amio {

// Helper wrapper around IOError.
class GenericError : public IOError
{
 public:
  GenericError(const char *fmt, ...);

  const char *Message() override {
    return message_;
  }
  int ErrorCode() override {
    return 0;
  }
  ErrorType Type() override {
    return ErrorType::Library;
  }

 private:
  ke::AutoArray<char> message_;
};

extern ke::Ref<GenericError> eOutOfMemory;
extern ke::Ref<GenericError> eUnknownHangup;
extern ke::Ref<GenericError> eTransportAlreadyRegistered;

} // namespace amio

#endif // _include_amio_shared_error_h_
