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

// Forward declarations.
class Poller;
class PosixTransport;
class StatusListener;

// The result of an IO operation.
struct AMIO_LINK IOResult
{
  // Set if there was an error.
  ke::Ref<IOError> error;

  // True if the operation completed; false if it would block.
  bool completed;

  // True if a connection has received an orderly shutdown from its peer. If
  // Ended is true, then the socket is automatically removed from the message
  // pump.
  bool ended;

  // Number of bytes that successfully completed. If 0 and Ended is false,
  // then no bytes were received or sent, and the caller should wait for
  // another read or write event to try again.
  size_t bytes;

  IOResult() : completed(false), ended(false), bytes(0)
  {}
};

// Describes a low-level transport mechanism used in Posix. This is essentially
// a wrapper around a file descriptor. Transports and their interactions with
// pollers are thread-safe, however, most operations are not atomic. For
// example, a Read() event on one thread can race with a ChangeEvents() call on
// another thread and lose an event. It is the user's responsiblity to
// synchronize as needed.
// 
// I/O operations may have undefined behavior if used on multiple threads. For
// example, it is inadvisable to have two threads writing to a stream at the
// same time.
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
  // will be false in IOResult, and the caller should wait for a notification
  // from the status listener to call again. (If the Read event is not enabled,
  // it is automatically enabled.)
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
  // will be false in IOResult, and the caller should wait for a notification
  // from the status listener to call again. (If the write event is not enabled,
  // it is automatically enabled).
  //
  // If an error occurs, |Error| will be set in |result|, and the result will
  // be false.
  virtual bool Write(IOResult *result, const void *buffer, size_t maxlength) = 0;

  // Closes the transport for further communication. This automatically
  // disconnects it from its active poller. Close() is automatically closed
  // when the transport has no more references, though if it is attached to
  // a poller, it will always have at least one reference until an EOF, error,
  // or hangup is received. It is recommended to explicitly close transports.
  //
  // If Close() is called on another thread, callbacks may still be fired
  // before the close is acknowledged.
  virtual void Close() = 0;

  // Return the file descriptor behind a transport. If it has been closed, this
  // will return -1.
  virtual int FileDescriptor() const = 0;

  // Returns whether or not the transport has been closed.
  virtual bool Closed() const = 0;

  // ETS event polling only.
  //
  // Signal to the underlying poller that a read operation would block. This
  // is needed in ETS mode when I/O operations outside of AMIO return EAGAIN or
  // EWOULDBLOCK is returned. For example, recvmsg().
  virtual PassRef<IOError> ReadIsBlocked() = 0;

  // ETS event polling only.
  //
  // Signal to the underlying poller that a write operation would block. This
  // is needed in ETS mode when I/O operations outside of AMIO return EAGAIN or
  // EWOULDBLOCK is returned. For example, sendmsg().
  virtual PassRef<IOError> WriteIsBlocked() = 0;

  // Return the listener associated with this transport.
  virtual PassRef<StatusListener> Listener() = 0;

  // Returns true if the listener is in proxy mode, false otherwise.
  virtual bool IsListenerProxying() = 0;

  // Internal function to cast transports to their underlying type.
  virtual PosixTransport *toPosixTransport() = 0;
};

// Used to receive notifications about status changes.
class AMIO_LINK StatusListener : public ke::IRefcounted
{
 public:
  virtual ~StatusListener()
  {}

  // Called when data is available for non-blocking reading. Always invoked
  // on the polling thread.
  virtual void OnReadReady()
  {}

  // Called when data is available for non-blocking sending. Always invoked
  // on the polling thread.
  virtual void OnWriteReady()
  {}

  // Called when a connection has been closed. If |error| is null, this is the
  // same as Read() returning a Closed status, however, some message pumps can
  // detect this as its own event and return it earlier.
  //
  // If |error| is non-null, the connection did not close gracefully.
  //
  // Once OnHangup() is called, the transport has already been detached from
  // its corresponding poller.
  virtual void OnHangup(ke::Ref<IOError> error)
  {}

  // This is only called in "proxy" mode, when the transport is detached, and
  // only when the detach is happening outside of a normal event. That is,
  // OnHangup() may be called instead of OnProxyDetach().
  virtual void OnProxyDetach()
  {}

  // This is only called if the listener is in "proxy" mode.
  virtual void OnChangeProxy(ke::Ref<StatusListener> new_listener)
  {}

  // This is only called if the listener is in "proxy" mode, and the set of
  // listened events changes.
  virtual void OnChangeEvents(Events new_events)
  {}
};

// An IODispatcher is responsible for dispatching IO events. This abstraction
// is provided separately from pollers for a very specific reason. While
// Pollers are ultimately the only tool for querying IO events, it is useful
// to build tools on top of pollers for dispatching events at different
// priority levels. Rather than hardcode this functionality directly into
// pollers, we allow building new dispatch mechanisms on top of pollers.
//
// Without this abstraction, it would be more difficult to layer helper
// functionality such as what amio::net provides.
//
// Dispatchers may not be thread-safe; check its derived classes to make sure.
class AMIO_LINK IODispatcher : public ke::IRefcounted
{
 public:
  virtual ~IODispatcher()
  {}

  // Attachs a transport. A transport can be registered to at most one
  // dispatcher at a time.
  //
  // The events bitmask specifies the initial events the poller will listen for
  // on this transport. It can be None. The mode specifies whether events
  // will be level- or edge-triggered.
  virtual PassRef<IOError> Attach(
    Ref<Transport> transport,
    Ref<StatusListener> listener,
    Events events,
    EventMode mode
  ) = 0;

  // Detachs a transport from the dispatcher. This happens automatically if the
  // transport is closed, a status error or hangup is generated, or a Read()
  // operation returns Ended. It is safe to deregister a transport multiple
  // times.
  //
  // Detaching a transport from a different dispatcher than it was attached to
  // is illegal and may crash.
  virtual void Detach(Ref<Transport> transport) = 0;

  // Changes the polled events on a transport. Event modes cannot be changed
  // without detaching and re-attaching the transport.
  virtual PassRef<IOError> ChangeEvents(Ref<Transport> transport, Events events) = 0;
  virtual PassRef<IOError> AddEvents(Ref<Transport> transport, Events events) = 0;
  virtual PassRef<IOError> RemoveEvents(Ref<Transport> transport, Events events) = 0;

  // Shuts down the dispatcher such that it will stop dispatching events.
  virtual void Shutdown() = 0;
};

// A poller is responsible for polling for events. Poller functions are
// thread-safe with respect to other calls on Poller or Transports. They are
// not atomic, meaning that operations can still race on different threads.
// We simply guarantee that the operations will not corrupt internal
// structures.
class AMIO_LINK Poller : public IODispatcher
{
 public:
  // The type of a listener, for notifications.
  typedef StatusListener Listener;

  // Poll for new events. If |timeoutMs| is greater than zero, Poll() may block
  // for at most that many milliseconds. If the message pump has no transports
  // registered, Poll() will exit immediately without an error.
  //
  // An error is returned if the poll itself failed; individual read/write
  // failures are propagated through status listeners.
  //
  // Poll() is not re-entrant. It may be called from other threads, though in
  // all current posix implementations this will block all but one call to
  // Poll().
  virtual PassRef<IOError> Poll(int timeoutMs = kNoTimeout) = 0;

  // Enables thread-safety on the poller. By default, pollers and attached
  // transports can only be used from one thread at a time.
  virtual void EnableThreadSafety() = 0;

  // Returns true if native edge-triggering support is available.
  virtual bool SupportsEdgeTriggering() = 0;

  // Returns the maximum number of threads that can concurrently call Poll(). If
  // this returns 0, there is no limit. If it returns 1, the poller is single-
  // threaded.
  virtual size_t MaximumConcurrency() = 0;
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
  static PassRef<IOError> Create(Ref<Poller> *outp);

  // Create a message pump based on select(). Although Windows supports select(),
  // AMIO uses IO Completion Ports which supports much more of the Windows API.
  // For now, select() pumps are not available on Windows.
  static PassRef<IOError> CreateSelectImpl(Ref<Poller> *outp);

#if defined(AMIO_POLL_AVAILABLE)
  // Create a message pump based on POSIX poll().
  static PassRef<IOError> CreatePollImpl(Ref<Poller> *outp);
#endif

#if defined(KE_LINUX)
  // Create a message pump based on epoll(). If maxEventsPerPoll is 0, then
  // the events per poll will be automatically sized. Otherwise, it will be
  // capped to the given value.
  //
  // epoll() is chosen by CreatePoller by default for Linux 2.5.44+, as it is
  // considered the most efficient polling mechanism and has native edge-
  // triggering.
  static PassRef<IOError> CreateEpollImpl(Ref<Poller> *outp, size_t maxEventsPerPoll = 0);
#elif defined(KE_BSD)
  // Create a message pump based on kqueue(). If maxEventsPerPoll is 0, then
  // the events per poll will be automatically sized. Otherwise, it will be
  // capped to the given value.
  //
  // kqueue() is chosen by CreatePoller by default for BSD (as it is
  // considered the most efficient polling mechanism and has native edge-
  // triggering). It is assumed to exist by default on Darwin, FreeBSD, and
  // OpenBSD.
  static PassRef<IOError> CreateKqueueImpl(Ref<Poller> *outp, size_t maxEventsPerPoll = 0);
#elif defined(KE_SOLARIS)
  // Create a message pump based on ports. By default maxEventsPerPoll is
  // 0, and the number of events per poll is automatically sized. Otherwise,
  // it will be capped to the given value.
  //
  // Completion ports are (currently) the only POSIX poller that supports
  // concurrent polling.
  static PassRef<IOError> CreateCompletionPort(Ref<Poller> *outp, size_t maxEventsPoll = 0);

  // Create a message pump based on /dev/poll. By default maxEventsPerPoll is
  // 0, and the number of events per poll is automatically sized. Otherwise,
  // it will be capped to the given value.
  static PassRef<IOError> CreateDevPollImpl(Ref<Poller> *outp, size_t maxEventsPerPoll = 0);
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

  // Internal flags. These must have the same values as any equivalent Event
  // counterpart.
  kTransportReading       = 0x00000004,
  kTransportWriting       = 0x00000008,
  kTransportLT            = 0x00000200,
  kTransportET            = 0x00000400,
  kTransportProxying      = 0x00001000,
  kTransportArmed         = 0x00010000,
  kTransportEventMask     = kTransportReading | kTransportWriting,
  kTransportUserFlagMask  = kTransportNoAutoClose|kTransportNoCloseOnExec,
  kTransportClearMask     = kTransportUserFlagMask,

  kTransportNoFlags       = 0x00000000,
  kTransportDefaultFlags  = kTransportNoFlags
};
KE_DEFINE_ENUM_OPERATORS(TransportFlags)

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
class AutoDisableSigPipe
{
 public:
  AutoDisableSigPipe();
  ~AutoDisableSigPipe();

 private:
  void (*prev_handler_)(int);
};

// Posix system calls can be interrupted on EINTR. This macro will automatically
// retry syscalls that fail for that reason. This is based on HANDLE_EINTR from
// Chromium.
#define AMIO_RETRY_IF_EINTR(expr)                 \
  ({                                              \
    __typeof__(expr) syscall_rv;                  \
    do {                                          \
      syscall_rv = expr;                          \
    } while (syscall_rv == -1 && errno == EINTR); \
    syscall_rv;                                   \
  })

} // namespace amio

#endif // _include_amio_posix_header_h_
