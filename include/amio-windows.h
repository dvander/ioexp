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

// The result of an IO operation. Testing the state of an IO operation typically
// should look like this:
//
//  IOResult r;
//  if (!transport->Read(&r, ...))
//     // return r.Error
//  ... exit back to the event loop.
// 
// However, it is possible to poll data faster by using the "Completed" bit
// in the result. If it is true, it is safe to immediately read or re-use
// bytes from the buffer given to the IO operation. However, unless the message
// pump has "ImmediateDelivery" enabled, the context may not be re-used
// for further operations until the corresponding event has been delivered to
// its listener. Since immediate delivery is not available on all versions of
// Windows, a fully-functional read loop might look like:
//
//  Ref<IOError> ProcessIO(Ref<Transport> transport, IOResult &r) {
//    if (!expecting_io_)
//      return nullptr;
//    ProcessBytesFrom(r);
//    while (true) {
//      expecting_io_ = false;
//
//      IOResult r;
//      if (!transport->Read(&r, r.context, ...))
//        return r.Error
//      if (!r.Completed)
//        return nullptr;
//
//      ProcessBytesFrom(r);
//
//      // Assuming we have one event active at a time, ignore the next event. In
//      // a system with multiple active operations per transport, we would have
//      // to communicate this through user data instead.
//      if (!r.Context) {
//        // Can't re-use the context, so wait until we get the event, then
//        // ignore it.
//        expecting_io_ = false;
//        return nullptr;
//      }
//    }
//  }
//
// If a system requires that immediate delivery is enabled, then there is no
// need to track whether or not the Context was set.
struct AMIO_CLASS IOResult
{
  // Set if there was an error.
  ke::Ref<IOError> Error;

  // True if a connection has received an orderly shutdown from its peer. If
  // Ended is true, then the socket is automatically removed from the poller.
  bool Ended;

  // If true, the operation completed immediately and |Bytes| contains the
  // number of bytes that completed. Any structures associated with the IO
  // operation may be freed or re-used.
  //
  // If false, the operation is still pending.
  bool Completed;

  // Number of bytes that successfully completed. If 0 and Ended is false,
  // then no bytes were received or sent, and the caller should wait for
  // another read or write event to try again.
  size_t Bytes;

  // If the operation completed successfully and the underlying IO event is
  // is no longer queued (that is, no notification will be passed to the
  // listener), then this will contain the context used to start the operation.
  // Otherwise, it is null.
  Ref<IOContext> Context;

  IOResult() : Ended(false), Completed(false), Bytes(0)
  {}
  IOResult(PassRef<IOError> error, PassRef<IOContext> context)
   : Error(error), Ended(false), Completed(false), Bytes(0), Context(context)
  {}
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

  // Initiates a read operation on the supplied buffer. If the operation fails
  // to initiate, then false is returned and r->Error is set. If the operation
  // cannot complete immediately, |r->Completed| will be false and all other
  // fields will be 0.
  //
  // If the operation can complete immediately, |r->Completed| will be true and
  // all fields will be filled as if they had come from the IOListener OnRead
  // event. If |r->Context| is non-null, then it is equal to |context| and no
  // event will be delivered through the poller. Otherwise, |context| is null
  // and the event will be delivered through the poller.
  //
  // Immediate delivery must be enabled in the poller, and is not available on
  // all versions of Windows.
  //
  // Contexts must not be re-used until an IOResult is returned with the
  // context, either through immediate completion or delivery through an event
  // listener.
  virtual bool Read(IOResult *r, ke::Ref<IOContext> context, void *buffer, size_t length) = 0;

  // Initiates a write operation using the supplied buffer. If the operation
  // fails to initiate, then false is returned and r->Error is set. If the
  // operation cannot complete immediately, |r->Completed| will be false and
  // all other fields will be 0.
  //
  // If the operation can complete immediately, |r->Completed| will be true and
  // all fields will be filled as if they had come from the IOListener OnWrite
  // event. If |r->Context| is non-null, then it is equal to |context| and no
  // event will be delivered through the poller. Otherwise, |context| is null
  // and the event will be delivered through the poller.
  //
  // Immediate delivery must be enabled in the poller, and is not available on
  // all versions of Windows.
  //
  // Contexts must not be re-used until an IOResult is returned with the
  // context, either through immediate completion or delivery through an event
  // listener.
  virtual bool Write(IOResult *r, ke::Ref<IOContext> context, const void *buffer, size_t length) = 0;

  // Helper version of Read() that automatically allocates a new context.
  //
  // An optional data value may be communicated through the event.
  IOResult Read(void *buffer, size_t length, uintptr_t data = 0);

  // Helper version of Write() that automatically allocates a new context.
  //
  // An optional data value may be communicated through the event.
  IOResult Write(const void *buffer, size_t length, uintptr_t data = 0);

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
  virtual void OnRead(ke::Ref<Transport> transport, IOResult &io)
  {}

  // Receives any write events posted from a Write() operation on a transport.
  virtual void OnWrite(ke::Ref<Transport> transport, IOResult &io)
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