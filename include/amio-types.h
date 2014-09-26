// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_types_h_
#define _include_amio_types_h_

#include <am-refcounting.h>
#include <am-refcounting-threadsafe.h>
#include <am-utility.h>

namespace amio {

#if defined(AMIO_IMPORT)
# define AMIO_CLASS KE_CLASS_IMPORT
#elif defined(AMIO_EXPORT)
# define AMIO_CLASS KE_CLASS_EXPORT
#else
# define AMIO_CLASS
#endif

using namespace ke;

// Types of errors that can occur.
#if defined(KE_CXX11)
enum class AMIO_CLASS ErrorType
{
  System,       // System error (code included).
  Library,      // Library (AMIO) error.
  Exception     // Generic exception.
};
#else
struct AMIO_CLASS ErrorType
{
  int type;
  static const ErrorType System;
  static const ErrorType Library;
  static const ErrorType Exception;
};
#endif

// Represents an I/O error.
class AMIO_CLASS IOError : public ke::RefcountedThreadsafe<IOError>
{
 public:
  virtual ~IOError()
  {}

  // A human-readable message describing the error.
  virtual const char *Message() = 0;

  // System error code. If none is available it will return 0.
  virtual int ErrorCode() = 0;

  // A general class the error falls into.
  virtual ErrorType Type() = 0;
};

// Specify no timeout in polling operations.
static const int kNoTimeout = -1;

} // namespace amio

#endif // _include_amio_types_h_