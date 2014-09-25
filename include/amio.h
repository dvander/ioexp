// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_header_h_
#define _include_amio_header_h_

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

// Wrapper around IOError to make tests with ! work.
struct MaybeError
{
  Ref<IOError> Error;

  explicit MaybeError()
  {}
  explicit MaybeError(Ref<IOError> error) : Error(error)
  {}
  operator bool() const {
    return !Error;
  }
};

// Flags that can be used for some TransportFactory functions.
enum AMIO_CLASS TransportFlags
{
  // Automatically close a transport. This is only relevant when a transport is
  // created from an existing operating system construct (such as a file
  // descriptor).
  kTransportAutoClose     = 0x00000001,

  kTransportNoFlags       =  0x00000000,
  kTransportDefaultFlags  =  kTransportAutoClose
};

class MessagePump;

// Underlying operating system types.
#if !defined(WIN32)
// Found in <amio/amio-posix-transport.h>
class PosixTransport;
#endif

// A Transport is an abstraction around reading and writing (for example,
// on POSIX systems, it is a file descriptor). Transports are always
// asynchronous.
class AMIO_CLASS Transport : public ke::Refcounted<Transport>
{
 public:
  virtual ~Transport()
  {}

  // Attempts to read a number of bytes from the transport into the provided
  // |buffer|, up to |maxlength| bytes. If any bytes are read, the number
  // of bytes is set in |result| accordingly.
  //
  // If the connection has been closed, |Closed| will be true in |result|.
  // 
  // If an error occurs, |Error| will be set in |result|, and the result will
  // be false.
  virtual bool Read(IOResult *result, uint8_t *buffer, size_t maxlength) = 0;

  // Attempts to write a number of bytes to the transport. If the transport
  // is connectionless (such as a datagram socket), all bytes are guaranteed
  // to be sent. Otherwise, only a partial number of bytes may be sent. The
  // number of bytes sent may be 0 without an error occurring.
  //
  // By default, message pumps do not listen for write events until a write
  // event would block. To initiate write status events, you must attempt to
  // call Write() at least once.
  //
  // If an error occurs, |Error| will be set in |result|, and the result will
  // be false.
  virtual bool Write(IOResult *result, const uint8_t *buffer, size_t maxlength) = 0;

  // Closes the transport, and frees any underlying operating system resources.
  // This does not free the C++ Transport object itself, which happens when
  // the object's reference count reaches 0.
  //
  // Close() is called automatically in the Transport's destructor.
  virtual void Close() = 0;

  // Internal functions to cast transports to their underlying types.
#if !defined(WIN32)
  virtual PosixTransport *toPosixTransport() = 0;
#endif
};

struct AMIO_CLASS MaybeTransport
{
  Ref<Transport> transport;
  Ref<IOError> error;

  MaybeTransport()
  {}
  explicit MaybeTransport(Ref<Transport> transport)
   : transport(transport)
  {}
  explicit MaybeTransport(Ref<IOError> error)
   : error(error)
  {}
};

class AMIO_CLASS TransportFactory
{
 public:
#if !defined(WIN32)
  // Create a transport from a pre-existing file descriptor.
  static MaybeTransport CreateFromDescriptor(int fd, TransportFlags flags = kTransportDefaultFlags);

  // Create a transport for a unix pipe (via the pipe() call).
  static MaybeError CreatePipe(Ref<Transport> *readerp, Ref<Transport> *writerp);
#endif
};

// Used to receive notifications about status changes. When an event 
class AMIO_CLASS StatusListener : public ke::Refcounted<StatusListener>
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

// Specify no timeout in Poll().
static const int kNoTimeout = -1;

// A message pump is responsible for receiving messages. It is not thread-safe.
class AMIO_CLASS MessagePump
{
 public:
   virtual ~MessagePump()
   {}

   // Initialize the message pump.
   virtual Ref<IOError> Initialize() = 0;

   // Poll for new events. If |timeoutMs| is greater than zero, Poll() may block
   // for at most that many milliseconds. If the message pump has no transports
   // registered, Poll() will exit immediately without an error.
   //
   // An error is returned if the poll itself failed; individual read/write
   // failures are propagated through status listeners.
   //
   // Poll() is not re-entrant.
   virtual Ref<IOError> Poll(int timeoutMs = kNoTimeout) = 0;

   // Interrupt a poll operation. The active poll operation will return an error.
   virtual void Interrupt() = 0;

   // Registers a transport with the pump. A transport can be registered to at
   // most one pump at any given time. Only transports created via
   // TransportFactory can be registered.
   virtual Ref<IOError> Register(Ref<Transport> transport, Ref<StatusListener> listener) = 0;

   // Deregisters a transport from a pump. This happens automatically if the
   // transport is closed, a status error or hangup is generated, or a Read()
   // operation returns Ended. It is safe to deregister a transport multiple
   // times.
   virtual void Deregister(Ref<Transport> transport) = 0;
};

// Creates message pumps.
class AMIO_CLASS MessagePumpFactory
{
  // Create a message pump using the best available polling technique. The pump
  // should be freed with |delete| or immediately stored in a ke::AutoPtr.
  static MessagePump *CreatePump();

#if !defined(WIN32)
  // Create a message pump based on select(). Although Windows supports select(),
  // AMIO uses IO Completion Ports which supports much more of the Windows API.
  // For now, select() pumps are not available on Windows.
  static MessagePump *CreateSelectPump();
#endif

#if defined(__linux__)
  // Create a message pump based on poll().
  static MessagePump *CreatePollPump();

  // Create a message pump based on epoll().
  static MessagePump *CreateEpollPump();
#elif defined(__APPLE__) || defined(BSD) || defined(__MACH__)
  // Create a message pump based on kqueue().
  static MessagePump *CreateKqueuePump();
#endif
};

} // namespace amio

#endif // _include_amio_header_h_
