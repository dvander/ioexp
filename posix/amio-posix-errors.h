// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_posix_errors_h_
#define _include_amio_posix_errors_h_

#include "include/amio-types.h"

namespace amio {

// Wrapper around errno.
class PosixError : public IOError
{
 public:
  PosixError(int errcode);

  const char *Message() override;
  int ErrorCode() override {
    return errcode_;
  }
  ErrorType Type() override {
    return ErrorType::System;
  }

 private:
  int errcode_;
  bool computed_message_;
  char message_[255];
};

} // namespace amio

#endif // _include_amio_posix_errors_h_
