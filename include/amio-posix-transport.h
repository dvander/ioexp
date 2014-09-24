// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_posix_transport_h_
#define _include_amio_posix_transport_h_

#include "include/amio.h"

#if defined(WIN32)
# error PosixTransport cannot be used on Windows.
#endif

namespace amio {

// Forward declaration.
class PosixPump;

// A PosixTransport wraps a Unix file descriptor.
class PosixTransport : public Transport
{
 public:
  PosixTransport(int fd, TransportFlags flags);
  ~PosixTransport();

  // Transport implementation.
  bool Read(IOResult *result, uint8_t *buffer, size_t maxlength) override;
  bool Write(IOResult *result, const uint8_t *buffer, size_t maxlength) override;
  void Close();
  PosixTransport *toPosixTransport() {
    return static_cast<PosixTransport *>(this);
  }

  // Return the underlying file descriptor.
  int fd() const {
    return fd_;
  }

  // Pump callbacks.
  void setPump(PosixPump *pump) {
    // Do not overwrite an existing pump with a non-null pump.
    assert(!pump_ || !pump);
    pump_ = pump;
  }
  PosixPump *pump() const {
    return pump_;
  }

 private:
  int fd_;
  TransportFlags flags_;
  PosixPump *pump_;
};

} // namespace amio

#endif // _include_amio_posix_transport_h_
