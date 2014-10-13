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
    public ke::RefcountedThreadsafe<PosixTransport>
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
    ke::RefcountedThreadsafe<PosixTransport>::AddRef();
  }
  void Release() override {
    ke::RefcountedThreadsafe<PosixTransport>::Release();
  }
  PassRef<IOError> ReadIsBlocked() override;
  PassRef<IOError> WriteIsBlocked() override;

  bool attached() {
    if (!poller_.get())
      return false;
    return true;
  }

  // Setup the descriptor, if it hasn't been set up already.
  PassRef<IOError> Setup();

  void attach(PosixPoller *poller, StatusListener *listener);
  void detach();

  int fd() const {
    return fd_;
  }
  AlreadyRefed<PosixPoller> poller() {
    return poller_.get();
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

  TransportFlags &flags() {
    return flags_;
  }

 private:
  int fd_;
  uintptr_t userdata_;
  TransportFlags flags_;

  // These should not cause cycles. When the transport is closed, or when the
  // poller removes transports, it forcibly nulls out these fields. However,
  // if a poller is never shutdown and it has a transport that never receives
  // an error/EOF/close, then they could hold eachother alive.
  AtomicRef<PosixPoller> poller_;
  Ref<StatusListener> listener_;
};

} // namespace amio

#endif // _include_amio_posix_transport_h_
