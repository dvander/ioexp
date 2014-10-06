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
# define AMIO_LINK KE_IMPORT
#elif defined(AMIO_EXPORT)
# define AMIO_LINK KE_EXPORT
#else
# define AMIO_LINK
#endif

#if !defined(_WIN32)
# define AMIO_POSIX
#endif

using namespace ke;

// Types of errors that can occur.
#if defined(KE_CXX11)
enum class ErrorType
{
  System,       // System error (code included).
  Library,      // Library (AMIO) error.
  Exception     // Generic exception.
};
#else
struct AMIO_LINK ErrorType
{
  int type;
  static const ErrorType System;
  static const ErrorType Library;
  static const ErrorType Exception;
};
#endif

enum EventFlags : uint32_t
{
  Event_Read  = 0x00000001,
  Event_Write = 0x00000002,

  Events_None = 0x00000000
};
static inline EventFlags operator |(const EventFlags &left, const EventFlags &right) {
  return EventFlags(uint32_t(left) | uint32_t(right));
}
static inline EventFlags operator &(const EventFlags &left, const EventFlags &right) {
  return EventFlags(uint32_t(left) & uint32_t(right));
}
static inline EventFlags operator ~(const EventFlags &flags) {
  return EventFlags(~uint32_t(flags));
}
static inline EventFlags & operator |=(EventFlags &left, const EventFlags &right) {
  return left = left | right;
}
static inline EventFlags & operator &=(EventFlags &left, const EventFlags &right) {
  return left = left & right;
}

// Represents an I/O error.
class AMIO_LINK IOError : public ke::RefcountedThreadsafe<IOError>
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
