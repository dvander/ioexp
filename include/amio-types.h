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
enum class ErrorType
{
  System,       // System error (code included).
  Library,      // Library (AMIO) error.
  Exception     // Generic exception.
};

// Flags for events.
enum class Events : uint32_t
{
  Read   =  0x4,
  Write  =  0x8,

  // These are used internally. They are not real events and do nothing if
  // specified.
  Hangup    = 0x100000,
  Queued    = 0x200000,
  Detached  = 0x400000,

  None   =  0x0
};
KE_DEFINE_ENUM_OPERATORS(Events)

// Event delivery modes.
enum class EventMode : uint32_t
{
  // In level-triggered mode, events will be delivered as long as they are
  // true. For example, as long as a write to a socket would not block, the
  // event will be delivered. This is supported on all pollers, and so it is
  // the default mode.
  Level  = 0x200,

  // In edge-triggered mode, events will be delivered only once their status
  // changes from not-true to true. For example, when a write to a socket
  // would not block, an event will be delivered. The event will not be
  // delivered again until the user encounters a situation which would cause
  // the write to block (for example, sending enough data to trigger EAGAIN).
  //
  // Edge triggering is more efficient than level-triggering since unnecessary
  // events not are fired. However, with huge volumes of I/O, naive use can
  // lead to starvation. If an event tries to deplete all available non-blocking
  // space, it may never yield to the main thread. It is usually a good idea
  // to perform some amount of event buffering with edge-triggered events.
  //
  // Edge triggering is not supported with select(), poll(), or Solaris devpoll
  // backends. Use Poller::SupportsEdgeTriggering() to test for this. Adding
  // an edge-triggered event to an incompatible poller will result in an error.
  Edge   = 0x400,

  // Some backends (noted above) do not support edge-triggering. AMIO's ETS
  // mode can emulate edge-triggering somewhat efficiently on these pollers,
  // so that developers can use edge-triggering on any platform, whether or
  // not native support is available. However, code which uses this mode must
  // obey an extra API contract:
  //
  //  Anytime a system call returns EAGAIN or EWOULDBLOCK, indicating that a
  //  read or write would block, Transport::ReadIsBlocked() or Transport::
  //  WriteIsBlocked must be called.
  //
  // This only applies to I/O operations that occur outside of Transport, such
  // as using recvmsg() on a transport's file descriptor. It is not needed for
  // normal calls to Transport::Read or Transport::Write. Failure to call
  // Read/WriteIsBlocked() otherwise will cause future events to not be
  // delivered.
  //
  // Note that ETS mode has the same pitfalls as Edge mode. If native edge-
  // triggering is available, then Read/WriteIsBlocked() methods are identical
  // to Poller::AddEvents() with Read or Write. There is a fast shortcut to
  // make these functions close to a no-op.
  ETS   = 0x800,

  // The proxy flag indicates that the status listener will be acting as a
  // proxy for another status listener, and should receive additional
  // notifications useful for maintaining proxy state.
  //
  // Note that modes are usually mutually exclusive, however, the proxying
  // mode may be or'd with other modes.
  Proxy  = 0x1000,

  // The default mode is level-triggered.
  Default = 0
};
KE_DEFINE_ENUM_OPERATORS(EventMode)

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

// User data that is reference counted.
class IUserData : public ke::IRefcounted
{
 public:
  virtual ~IUserData()
  {}
};

// Specify no timeout in polling operations.
static const int kNoTimeout = -1;

} // namespace amio

#endif // _include_amio_types_h_
