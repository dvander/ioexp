// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_windows_header_h_
#define _include_amio_windows_header_h_

#include <amio-types.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace amio {

// Forward declarations for internal types.
class WinTransport;
class WinContext;

// Forward declarations.
class IOListener;

// IO operations must be associated with a context object. A context can be
// associated with at most one operation at a time. Internally, this wraps
// an OVERLAPPED structure from WINAPI.
class IOContext : public ke::Refcounted<IOContext>
{
 public:
  virtual ~IOContext()
  {}

  // Pre-allocate an IOContext with given user data.
  static PassRef<IOContext> New(uintptr_t data = 0);

  // Return user data (defaults to 0). This should not be called while the
  // context is being used by an IO operation.
  virtual uintptr_t UserData() const = 0;

  // Set arbitrary user data. This should not be called while the context is
  // being used by an IO operation. The old user data is returned.
  virtual uintptr_t SetUserData(uintptr_t data) = 0;

  // Access to internal types.
  virtual WinContext *toWinContext() = 0;
};


// Information about a completed IO event.
struct IOEvent : public IOResult
{
  // The context for the operation. Normally this can be ignored, but it can
  // be used for comparing the context if multiple pre-allocated contexts are
  // in-flight.
  Ref<IOContext> Context;

  IOEvent(PassRef<IOContext> context, PassRef<IOError> error)
   : Context(context)
  {
    Error = error;
  }
};

// Describes a low-level transport mechanism used in Windows. All posted
// operations will be resolved in the order they are posted, however, the
// notification of completed events is not guaranteed to happen in-order.
// It is the application's responsibility to resolve this.
class AMIO_CLASS Transport : public ke::Refcounted<Transport>
{
 public:
  virtual ~Transport()
  {}

  // Initiates a read operation on the supplied buffer. The buffer must be held
  // alive until the listener is notified that the event has completed. If the
  // Read() operation immediately returns an error, then no IOEvent will be
  // posted.
  virtual PassRef<IOError> Read(ke::Ref<IOContext> context, void *buffer, size_t length) = 0;

  // Initiates a write operation on the supplied buffer. The buffer must be held
  // alive until the listener is notified that the event has completed. If the
  // Write() operation immediately returns an error, then no IOEvent will be
  // posted.
  virtual PassRef<IOError> Write(ke::Ref<IOContext> context, const void *buffer, size_t length) = 0;

  // Helper version of Read() that automatically allocates a new context.
  //
  // An optional data value may be communicated through the event.
  PassRef<IOError> Read(void *buffer, size_t length, uintptr_t data = 0);

  // Helper version of Write() that automatically allocates a new context.
  //
  // An optional data value may be communicated through the event.
  PassRef<IOError> Write(const void *buffer, size_t length, uintptr_t data = 0);

  // Close the transport, disconnecting it from any pollers.
  virtual void Close() = 0;

  // Returns true if the handle has been closed.
  virtual bool Closed() = 0;

  // Returns the underlying handle associated with this transport. If the
  // transport has been closed, INVALID_HANDLE_VALUE is returned instead.
  virtual HANDLE Handle() = 0;

  // Access to internal types.
  virtual WinTransport *toWinTransport() = 0;
};

// Status listeners for transports.
class AMIO_CLASS IOListener : public ke::Refcounted<IOListener>
{
 public:
  virtual ~IOListener()
  {}

  // Receives any read events posted from a Read() operation on a transport.
  virtual void OnRead(ke::Ref<Transport> transport, IOEvent &io)
  {}

  // Receives any write events posted from a Write() operation on a transport.
  virtual void OnWrite(ke::Ref<Transport> transport, IOEvent &io)
  {}
};

enum TransportFlags
{
  // Override the default behavior of transports and do not automatically
  // close their underlying operating system resources.
  kTransportNoAutoClose  = 0x00000001,

  kTransportDefaultFlags = 0x00000000
};

// Transport factory methods.
class AMIO_CLASS TransportFactory
{
 public:
  //Create a transport around an existing IO handle. The Handle must be
  // compatible with WriteFile/ReadFile and IO Completion Ports.
  static Ref<IOError> CreateFromHandle(
    Ref<Transport> *outp,
    HANDLE handle,
    TransportFlags flags = kTransportDefaultFlags
  );
};

} // namespace amio

#endif // _include_amio_windows_header_h_