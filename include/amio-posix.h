// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_posix_header_h_
#define _include_amio_posix_header_h_

#include <am-platform.h>

namespace amio {

// Forward declarations for internal types.
class PosixTransport;

// The result of an IO operation.
struct AMIO_LINK IOResult
{
  // Set if there was an error.
  ke::Ref<IOError> Error;

  // True if the operation completed; false if it would block.
  bool Completed;

  // True if a connection has received an orderly shutdown from its peer. If
  // Ended is true, then the socket is automatically removed from the message
  // pump.
  bool Ended;

  // Number of bytes that successfully completed. If 0 and Ended is false,
  // then no bytes were received or sent, and the caller should wait for
  // another read or write event to try again.
  size_t Bytes;

  IOResult() : Completed(false), Ended(false), Bytes(0)
  {}
};

// Describes a low-level transport mechanism used in Posix. This is essentially
// a wrapper around a file descriptor.
class AMIO_LINK Transport : public ke::IRefcounted
{
 public:
  virtual ~Transport()
  {}

  // Attempts to read a number of bytes from the transport into the provided
  // |buffer|, up to |maxlength| bytes. If any bytes are read, the number
  // of bytes is set in |result| accordingly. If the connection has been
  // closed, |Closed| will be true in |result|.
  //
  // If the operation cannot be completed without blocking, then |Completed|
  // wil be false in IOResult, and the caller should wait for a notification
  // from the status listener to call again.
  // 
  // If an error occurs, |Error| will be set in |result|, and the result will
  // be false.
  virtual bool Read(IOResult *result, void *buffer, size_t maxlength) = 0;

  // Attempts to write a number of bytes to the transport. If the transport
  // is connectionless (such as a datagram socket), all bytes are guaranteed
  // to be sent (unless the message it too large). Otherwise, only a partial
  // number of bytes may be sent. The number of bytes sent may be 0 without an
  // error occurring.
  //
  // If the operation cannot be completed without blocking, then |Completed|
  // wil be false in IOResult, and the caller should wait for a notification
  // from the status listener to call again.
  //
  // If an error occurs, |Error| will be set in |result|, and the result will
  // be false.
  virtual bool Write(IOResult *result, const void *buffer, size_t maxlength) = 0;

  // Closes the transport, and frees any underlying operating system resources.
  // This does not free the C++ Transport object itself, which happens when
  // the object's reference count reaches 0.
  //
  // Close() is called automatically in the Transport's destructor.
  virtual void Close() = 0;

  // Return the file descriptor behind a transport. If it has been closed, this
  // will return -1.
  virtual int FileDescriptor() const = 0;

  // Returns whether or not the transport has been closed.
  virtual bool Closed() const = 0;

  // Internal function to cast transports to their underlying type.
  virtual PosixTransport *toPosixTransport() = 0;
};

// Used to receive notifications about status changes.
class AMIO_LINK StatusListener : public ke::IRefcounted
{
 public:
  virtual ~StatusListener()
  {}

  // Called when data is available for non-blocking reading.
  virtual void OnReadReady(ke::Ref<Transport> transport)
  {}

  // Called when data is available for non-blocking sending.
  virtual void OnWriteReady(ke::Ref<Transport> transport)
  {}

  // Called when a connection has been closed by a peer. This is the same as
  // Read() returning a Closed status, however, some message pumps can detect
  // this as its own event and return it earlier.
  //
  // If this event has been received, the transport is automatically
  // deregistered beforehand.
  virtual void OnHangup(ke::Ref<Transport> transport)
  {}

  // Called when an error state is received.
  //
  // If this event has been received, the transport is automatically
  // deregistered beforehand.
  virtual void OnError(ke::Ref<Transport> transport, ke::Ref<IOError> error)
  {}
};

// Defined later.
class Poller;

// A IPollable is an object which can be polled. It is a pair of a transport
// and listener. It is a helper for higher-level layers which require using
// specific listeners to provide a cleaner API.
class IPollable
{
 public:
  virtual PassRef<IOError> Attach(Poller *poller) = 0;
};

// A poller is responsible for polling for events. It is not thread-safe.
class AMIO_LINK Poller : public ke::IRefcounted
{
 public:
  virtual ~Poller()
  {}

  // The type of a listener, for notifications.
  typedef StatusListener Listener;

  // Poll for new events. If |timeoutMs| is greater than zero, Poll() may block
  // for at most that many milliseconds. If the message pump has no transports
  // registered, Poll() will exit immediately without an error.
  //
  // An error is returned if the poll itself failed; individual read/write
  // failures are propagated through status listeners.
  //
  // Poll() is not re-entrant.
  virtual PassRef<IOError> Poll(int timeoutMs = kNoTimeout) = 0;

  // Interrupt a poll operation. The active poll operation will return an error.
  virtual void Interrupt() = 0;

  // Attachs a transport with the pump. A transport can be registered to at
  // most one pump at any given time. Only transports created via
  // TransportFactory can be registered.
  //
  // The eventMask specifies the initial events the poller will listen for
  // on this transport. Since all pollers simulate edge-triggering, it is the
  // caller's responsibility to request new events via Read() or Write().
  //
  // Because Read() and Write() automatically watch for events, it is not
  // necessary to pass any event flags here as long one of those will be
  // called.
  virtual PassRef<IOError> Attach(
    Ref<Transport> transport,
    Ref<StatusListener> listener,
    EventFlags eventMask
  ) = 0;

  // Detachs a transport from a pump. This happens automatically if the
  // transport is closed, a status error or hangup is generated, or a Read()
  // operation returns Ended. It is safe to deregister a transport multiple
  // times.
  virtual void Detach(Ref<Transport> transport) = 0;

  // Changes the polled events on a transport. This is only valid for level-
  // triggered listeners. If the transport was not attached with Event_Sticky,
  // this will fail. The new event mask must contain Event_Sticky.
  //
  // NB: This is provided so embedders can simulate edge-triggering as needed;
  // thus, there is no edge-triggered equivalent.
  virtual PassRef<IOError> ChangeStickyEvents(Ref<Transport> transport, EventFlags eventMask) = 0;

  // Helper for IPollables.
  PassRef<IOError> Attach(IPollable *pollable);
};

#if defined(KE_BSD) || defined(KE_LINUX) || defined(KE_SOLARIS)
# define AMIO_POLL_AVAILABLE
#endif

#if defined(KE_SOLARIS)
class CompletionPort;
#endif

// Creates message pumps.
class AMIO_LINK PollerFactory
{
 public:
  // Create a message pump using the best available polling technique. The pump
  // should be freed with |delete| or immediately stored in a ke::AutoPtr.
  static PassRef<IOError> CreatePoller(Ref<Poller> *outp);

  // Create a message pump based on select(). Although Windows supports select(),
  // AMIO uses IO Completion Ports which supports much more of the Windows API.
  // For now, select() pumps are not available on Windows.
  static PassRef<IOError> CreateSelectImpl(Ref<Poller> *outp);

#if defined(AMIO_POLL_AVAILABLE)
  // Create a message pump based on POSIX poll().
  static PassRef<IOError> CreatePollImpl(Ref<Poller> *outp);
#endif

#if defined(KE_LINUX)
  // Create a message pump based on epoll(). By default maxEventsPerPoll is 
  // 256, when used through CreatePoller(). Use 0 for the default.
  //
  // epoll() is chosen by CreatePoller by default for Linux 2.5.44+, as it is
  // considered the most efficient polling mechanism and has native edge-
  // triggering.
  static PassRef<IOError> CreateEpollImpl(Ref<Poller> *outp, size_t maxEventsPerPoll = 0);
#elif defined(KE_BSD)
  // Create a message pump based on kqueue(). By default maxEventsPerPoll is
  // 256, when used through CreatePoller(). Use 0 for the default.
  //
  // kqueue() is chosen by CreatePoller by default for BSD (as it is
  // considered the most efficient polling mechanism and has native edge-
  // triggering). It is assumed to exist by default on Darwin, FreeBSD, and
  // OpenBSD.
  static PassRef<IOError> CreateKqueueImpl(Ref<Poller> *outp, size_t maxEventsPerPoll = 0);
#elif defined(KE_SOLARIS)
  // Create a message pump based on /dev/poll. By default maxEventsPerPoll is
  // 256. Use 0 for the default. CreatePoller() never chooses /dev/poll.
  static PassRef<IOError> CreateDevPollImpl(Ref<Poller> *outp, size_t maxEventsPerPoll = 0);

  // Create a message pump based on IO Completion Ports. By default
  // maxEventsPerPoll is 256, when used through CreatePoller(). Use 0 for the
  // default.
  //
  // Unlike Windows IOCP, Solaris allows normal poll()-like notifications
  // through the port, which can simulate edge-triggering natively. AMIO will
  // choose completion ports by default for Solaris.
  //
  // Although Solaris offers Windows-like async I/O through its AIO interface,
  // this is not yet supported (AIO is geared toward direct access to block
  // devices, AMIO is intended for IPC and sockets). Additionally, since it
  // would (probably) require transport-level locking, this poller is not
  // thread-safe even though the underlying port is.
  static PassRef<IOError> CreateCompletionPort(Ref<Poller> *outp, size_t maxEventsPoll = 0);
#endif
};

// Flags that can be used for some TransportFactory functions.
enum AMIO_LINK TransportFlags
{
  // Automatically close a transport. This is only relevant when a transport is
  // created from an existing operating system construct (such as a file
  // descriptor).
  kTransportNoAutoClose   = 0x00000001,

  // Don't set FD_CLOEXEC. By default, all transports have this flag, even if
  // the transport does not own the descriptor.
  kTransportNoCloseOnExec = 0x00000002,

  kTransportDefaultFlags  = 0x00000000
};

class AMIO_LINK TransportFactory
{
 public:
  // Create a transport from a pre-existing file descriptor. If this fails, the
  // descriptor is left open even if flags does no contain kTransportNoAutoClose.
  static PassRef<IOError> CreateFromDescriptor(Ref<Transport> *outp, int fd, TransportFlags flags = kTransportDefaultFlags);

  // Create a transport for a unix pipe (via the pipe() call).
  static PassRef<IOError> CreatePipe(Ref<Transport> *readerp, Ref<Transport> *writerp,
                                     TransportFlags flags = kTransportDefaultFlags);
};

// This class can be used to automatically disable SIGPIPE. By default, Poll()
// will not disable SIGPIPE.
class AutoDisableSigpipe
{
 public:
  AutoDisableSigpipe();
  ~AutoDisableSigpipe();

 private:
  void (*prev_handler_)(int);
};

} // namespace amio

#endif // _include_amio_posix_header_h_
