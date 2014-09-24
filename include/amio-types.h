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

// Types of errors that can occur.
enum class ErrorType
{
  // System error.
  System,

  // Library error.
  Library,

  // Generic I/O exception.
  Exception
};

// Represents an I/O error.
class IOError : public ke::Refcounted<IOError>
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
struct IOResult
{
  // Set if there was an error.
  ke::Ref<IOError> Error;

  // True if the connection was closed.
  bool Closed;

  // Number of bytes that successfully completed.
  size_t Bytes;

  IOResult() : Closed(false), Bytes(0)
  {}
};

// A Transport is an abstraction around reading and writing (for example,
// on POSIX systems, it is a file descriptor). Transports are always
// asynchronous.
class Transport : public ke::Refcounted<Transport>
{
 public:
  virtual ~Transport()
  {}

  // Attempts to read a number of bytes from the transport into the provided
  // |buffer|, up to |maxlength| bytes. If any bytes are read, the number
  // of bytes is returned and set in |result| accordingly.
  //
  // If the connection has been closed, |Closed| will be true in |result|.
  // 
  // If an error occurs, |Error| will be set in |result|, and the result will
  // be -1.
  virtual ssize_t Read(IOResult *result, uint8_t *buffer, size_t maxlength) = 0;

  // Attempts to write a number of bytes to the transport. If the transport
  // is connectionless (such as a datagram socket), all bytes are guaranteed
  // to be sent. Otherwise, only a partial number of bytes may be sent. The
  // number of bytes sent may be 0 without an error occurring.
  //
  // If an error occurs, |Error| will be set in |result|, and the result will
  // be -1.
  virtual ssize_t Write(IOResult *result, const uint8_t *buffer, size_t maxlength) = 0;

  // Closes the transport, and frees any underlying operating system resources.
  // This does not free the C++ Transport object itself, which happens when
  // the object's reference count reaches 0.
  //
  // Close() is called automatically in the Transport's destructor.
  virtual void Close() = 0;
};

// Used to receive notifications about status changes. All status notifications
// are edge-triggered, meaning the status notification happens once, and it is
// the user's responsibility to consume data.
class StatusListener : public ke::Refcounted<StatusListener>
{
 public:
  virtual ~StatusListener()
  {}

  // Called when data is available for non-blocking reading.
  virtual void OnReadReady(ke::Ref<Transport> transport) = 0;

  // Called when data is available for non-blocking sending.
  virtual void OnWriteReady(ke::Ref<Transport> transport) = 0;

  // Called when the connection is closed.
  virtual void OnHangup(ke::Ref<Transport> transport) = 0;

  // Called when an error state is received.
  virtual void OnError(ke::Ref<Transport> transport, ke::Ref<IOError> error) = 0;
};

} // namespace amio

#endif // _include_amio_types_h_

