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
#include "include/amio-posix.h"

#if defined(WIN32)
# error PosixTransport cannot be used on Windows.
#endif

namespace amio {

// Forward declaration.
class PosixPoller;

// A PosixTransport wraps a Unix file descriptor.
class PosixTransport : public Transport
{
 public:
  PosixTransport(int fd, TransportFlags flags);
  ~PosixTransport();

  // Transport implementation.
  bool Read(IOResult *result, uint8_t *buffer, size_t maxlength) override;
  bool Write(IOResult *result, const uint8_t *buffer, size_t maxlength) override;
  void Close() override;

  PosixTransport *toPosixTransport() override {
    return static_cast<PosixTransport *>(this);
  }
  int FileDescriptor() const override {
    return fd_;
  }
  bool Closed() const override {
    return fd_ == -1;
  }

  // Return the underlying file descriptor.
  int fd() const {
    return fd_;
  }

  // Pump callbacks.
  void setPump(PosixPoller *pump) {
    // Do not overwrite an existing pump with a non-null pump.
    assert(!pump_ || !pump);
    pump_ = pump;
  }
  PosixPoller *pump() const {
    return pump_;
  }

  // These are used by message pumps; they should not be called from outside.
  void setUserData(uintptr_t userdata) {
    userdata_ = userdata;
  }
  uintptr_t getUserData() const {
    return userdata_;
  }

 private:
  int fd_;
  uintptr_t userdata_;
  TransportFlags flags_;
  PosixPoller *pump_;
};

} // namespace amio

#endif // _include_amio_posix_transport_h_
