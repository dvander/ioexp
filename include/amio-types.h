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
enum class AMIO_CLASS ErrorType
{
  System,       // System error (code included).
  Library,      // Library (AMIO) error.
  Exception     // Generic exception.
};

// Represents an I/O error.
class AMIO_CLASS IOError : public ke::Refcounted<IOError>
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

// The result of an IO operation.
struct AMIO_CLASS IOResult
{
  // Set if there was an error.
  ke::Ref<IOError> Error;

  // True if a connection has received an orderly shutdown from its peer. If
  // Ended is true, then the socket is automatically removed from the message
  // pump.
  bool Ended;

  // Number of bytes that successfully completed. If 0 and Ended is false,
  // then no bytes were received or sent, and the caller should wait for
  // another read or write event to try again.
  size_t Bytes;

  IOResult() : Ended(false), Bytes(0)
  {}
};

// Specify no timeout in polling operations.
static const int kNoTimeout = -1;

} // namespace amio

#endif // _include_amio_types_h_