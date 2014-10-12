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

#if defined(_WIN32)
# error PosixTransport cannot be used on Windows.
#endif

namespace amio {

// Forward declaration.
class PosixPoller;

// A PosixTransport wraps a Unix file descriptor.
class PosixTransport
  : public Transport,
    public ke::Refcounted<PosixTransport>
{
 public:
  PosixTransport(int fd, TransportFlags flags);
  ~PosixTransport();

  // Transport implementation.
  bool Read(IOResult *result, void *buffer, size_t maxlength) override;
  bool Write(IOResult *result, const void *buffer, size_t maxlength) override;
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
  void AddRef() override {
    ke::Refcounted<PosixTransport>::AddRef();
  }
  void Release() override {
    ke::Refcounted<PosixTransport>::Release();
  }

  // Setup the descriptor, if it hasn't been set up already.
  PassRef<IOError> Setup();

  // Return the underlying file descriptor.
  int fd() const {
    return fd_;
  }

  // Pump callbacks.
  void attach(PosixPoller *pump, PassRef<StatusListener> listener) {
    // Do not overwrite an existing pump with a non-null pump.
    assert(!pump_);
    pump_ = pump;
    listener_ = listener;
  }
  void detach() {
    pump_ = nullptr;
    listener_ = nullptr;
  }
  PosixPoller *pump() const {
    return pump_;
  }
  PassRef<StatusListener> listener() {
    return listener_;
  }
  void changeListener(Ref<StatusListener> listener) {
    listener_ = listener;
  }

  // These are used by message pumps; they should not be called from outside.
  void setUserData(uintptr_t userdata) {
    userdata_ = userdata;
  }
  uintptr_t getUserData() const {
    return userdata_;
  }

  void ReadIsBlocked() override;
  void WriteIsBlocked() override;

 private:
  int fd_;
  uintptr_t userdata_;
  TransportFlags flags_;
  PosixPoller *pump_;

  // This will not cause a cycle with status listeners that hold a reference to
  // the transport, since the act of closing the transport (which is recommended)
  // will clear the pump, which also clears the listener.
  Ref<StatusListener> listener_;
};

} // namespace amio

#endif // _include_amio_posix_transport_h_
