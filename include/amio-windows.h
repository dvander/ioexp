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
#define FD_SETSIZE 256
#include <Windows.h>
#include <WinSock2.h>

namespace amio {

// Forward declarations for internal types.
class WinTransport;
class WinContext;
class WinBasePoller;

// Forward declarations.
class IOListener;
class Transport;
class Poller;

enum class RequestType
{
  None,
  Cancelled,
  Message,

  // Generic operations.
  Read,
  Write,
  Other
};

// IO operations must be associated with a context object. A context can be
// associated with at most one operation at a time. Internally, this wraps
// an OVERLAPPED structure from WINAPI.
class IOContext : public ke::RefcountedThreadsafe<IOContext>
{
 public:
  virtual ~IOContext()
  {}

  // Pre-allocate an IOContext with the given user value.
  static PassRef<IOContext> New(uintptr_t value = 0);

  // Pre-allocate an IOContext with the given user data and value.
  static PassRef<IOContext> New(Ref<IUserData> data, uintptr_t value = 0);

  // Return user data (defaults to 0). This should not be called while the
  // context is being used by an IO operation.
  virtual uintptr_t UserValue() const = 0;

  // Set arbitrary user data. This should not be called while the context is
  // being used by an IO operation. The old user data is returned.
  virtual uintptr_t SetUserValue(uintptr_t value) = 0;

  // Return user data (defaults to null).
  virtual PassRef<IUserData> UserData() const = 0;

  // Set user data.
  virtual void SetUserData(Ref<IUserData> data) = 0;

  // Access to internal types.
  virtual WinContext *toWinContext() = 0;
};

// Helper class for custom consumers of OVERLAPPED *.
class BeginOverlappedRequest
{
 public:
  // This binds the context for the specified event until the event has been
  // posted back to the completion port, or Cancel() is called on this request
  // object.
  //
  // The request must be "Read", "Write", or "Other"; the transport must be
  // attached to a poller; and the context must not already be bound. If any
  // any of those conditions are not met, overlapped() will return null.
  BeginOverlappedRequest(Ref<Transport> transport, Ref<IOContext> context, RequestType type);

  // Return the OVERLAPPED * that should be used for the request.
  OVERLAPPED *overlapped() const;

  // Contexts are automatically unbound once they have been received through
  // an IO completion port. However, if an operation fails and no event is
  // enqueued, it must be manually unbound. For example:
  //
  //    BeginOverlappedRequest request(transport, context, RequestType::Other);
  //    if (!SomeAsyncWinAPICall(blah, request.overlapped())) {
  //      request.Cancel();
  //    ...
  //
  // The request should not be cancelled under any other circumstance.
  void Cancel();

 private:
  Ref<Poller> poller_;
  Ref<Transport> transport_;
  Ref<IOContext> context_;
};

// The result of an IO operation. Testing the state of an IO operation typically
// should look like this:
//
//  IOResult r;
//  if (!transport->Read(&r, ...))
//     // return r.Error
//  ... exit back to the event loop.
// 
// However, it is possible to poll data faster by using the "completed" bit
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
//      if (!r.completed)
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
struct IOResult
{
  // Set if there was an error.
  ke::Ref<IOError> error;

  // True if a connection has received an orderly shutdown from its peer. If
  // ended is true, then the socket is automatically removed from the poller.
  bool ended;

  // When reading messages from a message-based stream (such as message pipes,
  // or UDP), and the given buffer was not large enough to read the entire
  // message, one of these will be true. Whether or not the remaining data can
  // be read depends on which is set. moreData indicates that it can; truncated
  // indicates that it cannot.
  bool moreData;
  bool truncated;

  // If true, the operation completed immediately and |bytes| contains the
  // number of bytes that completed. Any structures associated with the IO
  // operation may be freed or re-used.
  //
  // If false, the operation is still pending.
  bool completed;

  // Number of bytes that successfully completed. If 0 and ended is false,
  // then no bytes were received or sent, and the caller should wait for
  // another read or write event to try again.
  size_t bytes;

  // If the operation completed successfully and the underlying IO event is
  // is no longer queued (that is, no notification will be passed to the
  // listener), then this will contain the context used to start the operation.
  // Otherwise, it is null.
  Ref<IOContext> context;

  IOResult()
   : ended(false), moreData(false), truncated(false), completed(false), bytes(0)
  {}
  IOResult(PassRef<IOError> error, PassRef<IOContext> context)
   : error(error), ended(false), moreData(false), truncated(false),
     completed(false), bytes(0), context(context)
  {}
};

// Describes a low-level transport mechanism used in Windows. All posted
// operations will be resolved in the order they are posted, however, the
// notification of completed events is not guaranteed to happen in-order.
// It is the application's responsibility to resolve this.
//
// Unlike the Transport in the POSIX API, Pollers do not implicitly own a
// reference to Windows Transports. IOContexts do, however. A transport that
// is attached to a poller, but has no references and no active I/O operations,
// will be closed and detached.
//
// Transport operations are threadsafe with respect to itself and its attached
// Poller unless otherwise noted.
class AMIO_LINK Transport : public ke::RefcountedThreadsafe<Transport>
{
 public:
  virtual ~Transport()
  {}

  // Initiates a read operation on the supplied buffer. If the operation fails
  // to initiate, then false is returned and r->Error is set. If the operation
  // cannot complete immediately, |r->completed| will be false and all other
  // fields will be 0.
  //
  // If the operation can complete immediately, |r->completed| will be true and
  // all fields will be filled as if they had come from the IOListener OnRead
  // event. If |r->context| is non-null, then it is equal to |context| and no
  // event will be delivered through the poller. Otherwise, |context| is null
  // and the event will be delivered through the poller.
  //
  // Contexts must not be re-used until an IOResult is returned with the
  // context, either through immediate completion or delivery through an event
  // listener.
  //
  // Transports must be attached to call Read().
  virtual bool Read(IOResult *r, ke::Ref<IOContext> context, void *buffer, size_t length) = 0;

  // Initiates a write operation using the supplied buffer. If the operation
  // fails to initiate, then false is returned and r->Error is set. If the
  // operation cannot complete immediately, |r->completed| will be false and
  // all other fields will be 0.
  //
  // If the operation can complete immediately, |r->completed| will be true and
  // all fields will be filled as if they had come from the IOListener OnWrite
  // event. If |r->context| is non-null, then it is equal to |context| and no
  // event will be delivered through the poller. Otherwise, |context| is null
  // and the event will be delivered through the poller.
  //
  // Contexts must not be re-used until an IOResult is returned with the
  // context, either through immediate completion or delivery through an event
  // listener.
  //
  // Transports must be attached to call Write().
  virtual bool Write(IOResult *r, ke::Ref<IOContext> context, const void *buffer, size_t length) = 0;

  // Helper version of Read() that automatically allocates a new context.
  //
  // An optional data value may be communicated through the event.
  //
  // Thread-safe with respect to other Transport operations, and Poller::Poll().
  IOResult Read(void *buffer, size_t length, uintptr_t data = 0);

  // Helper version of Write() that automatically allocates a new context.
  //
  // An optional data value may be communicated through the event.
  //
  // Thread-safe with respect to other Transport operations, and Poller::Poll().
  IOResult Write(const void *buffer, size_t length, uintptr_t data = 0);

  // Cancel an in-progress IO operation. This may be called from any thread.
  // Contexts in-use with Post() cannot be cancelled. Cancel() has no effect
  // if the transport has been closed.
  virtual void Cancel(Ref<IOContext> context) = 0;

  // Close the transport. This does not guarantee any pending IO events will
  // be cancelled; events may still be posted to the poller if a transport is
  // closed before all pending operations complete. However, the poller will
  // discard any events received on closed transports.
  // 
  // If you have allocated data associated with a context, it is best to use
  // IUserData so it is always freed.
  virtual void Close() = 0;

  // Returns true if the handle has been closed.
  virtual bool Closed() = 0;

  // Returns the underlying handle associated with this transport. If the
  // transport has been closed, INVALID_HANDLE_VALUE is returned instead.
  //
  // IO operations must not be performed outside of the Transport API,
  // otherwise the poller will (almost certainly) crash.
  virtual HANDLE Handle() = 0;

  // Returns true if immediate delivery has been enabled on this transport. See
  // Poller for more details.
  virtual bool ImmediateDelivery() const = 0;

  // Access to internal types.
  virtual WinTransport *toWinTransport() = 0;
};

// Status listeners for transports.
class AMIO_LINK IOListener : public ke::IRefcounted
{
 public:
  virtual ~IOListener()
  {}

  // Receives any read events posted from a Read() operation on a transport.
  virtual void OnRead(IOResult &io)
  {}

  // Receives any write events posted from a Write() operation on a transport.
  virtual void OnWrite(IOResult &io)
  {}

  // Receives events initiated through WinAPI with an "Other" context request.
  virtual void OnCompleted(IOResult &io)
  {}
};

enum TransportFlags
{
  // Override the default behavior of transports and do not automatically
  // close their underlying operating system resources.
  kTransportNoAutoClose       = 0x00000001,
  
  // Use immediate delivery. This cannot be specified when instantiating
  // transports from existing handles.
  kTransportImmediateDelivery = 0x00000002,

  kTransportDefaultFlags = 0x00000000
};

// Transport factory methods.
class AMIO_LINK TransportFactory
{
 public:
  // Create a transport around an existing IO handle. The Handle must be
  // compatible with WriteFile/ReadFile and IO Completion Ports. IO operations
  // must not be performed outside of the transport API.
  static PassRef<IOError> CreateFromFile(
    Ref<Transport> *outp,
    HANDLE handle,
    TransportFlags flags = kTransportDefaultFlags
  );

  // Creates a transport around an existing socket.
  static PassRef<IOError> CreateFromSocket(
    Ref<Transport> *outp,
    SOCKET socket,
    TransportFlags flags = kTransportDefaultFlags
  );

  static PassRef<IOError> CreatePipe(Ref<Transport> *readerp, Ref<Transport> *writerp,
                                     TransportFlags flags = kTransportDefaultFlags);
};

class AMIO_LINK IODispatcher : public ke::IRefcounted
{
 public:
  virtual ~IODispatcher()
  {}

  // Register a transport to an IO listener. The transport is permanently
  // attached to the poller; it is automatically removed when the transport
  // is closed.
  //
  // Attach() may be called from any thread.
  virtual PassRef<IOError> Attach(Ref<Transport> transport, Ref<IOListener> listener) = 0;
};

// The primary, if only way to implement true I/O multiplexing on Windows is
// with I/O Completion Ports. Thus, the API for Poller maps 1:1 onto IOCP
// behavior.
//
// Note that contexts are not destroyed when a poller is destroyed, because
// of the way asynchronous I/O works in Windows. Pending events may be sitting
// in the kernel's queue, or in the poller's queue. It is the user's
// responsibility to make sure these are flushed, otherwise once the poller is
// deleted or no longer in use, the C++ memory for the contexts (and their
// transports) will be leaked.
//
// Functions are not thread-safe unless otherwise noted.
class AMIO_LINK Poller : public IODispatcher
{
 public:
  // The type of a listener, for notifications.
  typedef IOListener Listener;

  // Poll for new notifications. If timeoutMs is 0, then polling will not
  // block. If timeoutMs is greater than 0, it may block for at most that many
  // milliseconds. If timeoutMs is kNoTimeout, it may block indefinitely.
  virtual PassRef<IOError> Poll(int timeoutMs = kNoTimeout) = 0;

  // Same as Poll(), except at most one event will be processed. This is the
  // default behavior on operating systems before Windows Vista.
  virtual PassRef<IOError> PollOne(int timeoutMs = kNoTimeout) = 0;

  // Wait for all pending IO events to complete, and discard them. This will
  // ensure that no memory is leaked before deleting a Poller. All other polling
  // threads should be stopped before calling this function. This will also
  // block until all events have been received.
  virtual void WaitAndDiscardPendingEvents() = 0;

  // Attempt to put attached transports into immediate delivery mode. If
  // immediate delivery mode is enabled, Read() and Write() calls may return
  // immediately (if able) without posting an event to the poller. Immediate
  // delivery is only available on Windows Vista or higher, or Windows Server
  // 2008 or higher.
  //
  // It is recommended to use this mode when possible as it means less events
  // being delivered. However, code must be careful to account for when it is
  // not available (see the comment above IOResult). Code must also be careful
  // not to starve a non-polling thread by continually reading data.
  //
  // Returns true on success, false if immediate delivery mode is not
  // available.
  virtual bool EnableImmediateDelivery() = 0;

  // This is the same as EnableImmediateDelivery, except that if a transport
  // cannot enter immediate delivery mode, it will fail to attach. This allows
  // you to handle less edge cases.
  virtual bool RequireImmediateDelivery() = 0;

  // Enable multi-threading on this poller.
  virtual void EnableThreadSafety() = 0;

  // Post an event directly into the poller. The context must not be in-use. On
  // success, it will be returned in a future call to Poll() or PollOnce()
  // through IOListener::OnCompleted. Until that time, the context is in-use
  // and may not be re-used except for future calls to Post().
  //
  // The IOResult will have bytes = 0, ended = false, completed = true,
  // moreData = true, and completed = true.
  virtual PassRef<IOError> Post(Ref<IOContext> context, Ref<IOListener> listener) = 0;

  // Cast to internal types.
  virtual WinBasePoller *toWinBasePoller() = 0;
};

class AMIO_LINK PollerFactory
{
 public:
  // Create a poller. By default the backing implementation uses IOCP
  // (I/O Completion Ports) for a single thread, using the default batch
  // version if supported.
  static PassRef<IOError> Create(Ref<Poller> *poller);

  // Create an I/O Completion Port poller. The maximum number of threads that
  // can poll without blocking can be specified with nConcurrentThreads. If
  // nMaxEventsPerPoll is greater than 0, the poller will limit how many events
  // it will dequeue per poll.
  static PassRef<IOError> CreateCompletionPort(Ref<Poller> *poller, size_t nConcurrentThreads, size_t nMaxEventsPerPoll = 0);
};

} // namespace amio

#endif // _include_amio_windows_header_h_
