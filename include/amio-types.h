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

#define AMIO_LINK KE_LINK

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
  Event_Read   =  0x4,
  Event_Write  =  0x8,

  // Normally, events are cleared after they are received, and the user must
  // deplete the I/O buffer to signal that a new event is needed. This is called
  // "edge-triggered" behavior. While edge-triggering is very efficient, huge
  // volumes of I/O depletion can starve the I/O thread. One way around this is
  // to buffer I/O events in a separate queue, and incrementally process that
  // queue in your Poll() loop.
  //
  // "Level-triggered" polling makes this easier. In level-trigger mode, events
  // are not cleared, and will signal for as long as their condition is true.
  // AMIO exposes level-triggering as the Event_Sticky flag.
  //
  // select(), WSASelect(), poll(), and WSAPoll() are stateless; AMIO emulates
  // both behavior modes when using them as pollers.
  //
  // epoll() and kqueue() have native support for both edge- and level-
  // triggered behavior*.
  //
  // On Solaris, completion ports are (effectively) stateless. AMIO emulates
  // both behaviors (level-triggering is more expensive as the port must be
  // re-armed more often). /dev/poll is level-triggered; edge-triggering is
  // emulated.
  //
  // *Note: There is one flag to control behavior for both reads and writes.
  // Although most pollers can distinguish between the modes on one transport,
  // epoll() cannot, so for simplicity we do not provide an API for it.
  Event_Sticky =  0x10,

  Events_None  =  0x0
};
KE_DEFINE_ENUM_OPERATORS(EventFlags)

// Represents an I/O error.
class IOError : public ke::RefcountedThreadsafe<IOError>
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
