// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
#ifndef _include_amio_windows_transport_h_
#define _include_amio_windows_transport_h_

#include <amio.h>

namespace amio {

using namespace ke;

class WinBasePoller;

class WinTransport : public Transport
{
 public:
  WinTransport(TransportFlags flags);

  WinTransport *toWinTransport() override {
    return this;
  }
  virtual void Close() override;

  bool Read(IOResult *r, Ref<IOContext> context, void *buffer, size_t length) override;
  bool Write(IOResult *r, Ref<IOContext> context, const void *buffer, size_t length) override;
  void Cancel(Ref<IOContext> context) override;

  // This is only called if the poller has determined ahead of time that
  // immediate delivery can be used.
  virtual PassRef<IOError> EnableImmediateDelivery() = 0;

  // This is so GetQueuedCompletionStatusEx() can get error codes.
  virtual DWORD GetOverlappedError(OVERLAPPED *ovp) = 0;

  virtual bool read(IOResult *r, WinBasePoller *poller,  WinContext *context, void *buffer, size_t length) = 0;
  virtual bool write(IOResult *r, WinBasePoller *poller,  WinContext *context, const void *buffer, size_t length) = 0;

  PassRef<IOListener> listener() const {
    return listener_;
  }
  AlreadyRefed<WinBasePoller> get_poller() {
    return poller_.get();
  }
  bool ImmediateDelivery() const {
    return !!(flags_ & kTransportImmediateDelivery);
  }
  void attach(PassRef<WinBasePoller> poller, PassRef<IOListener> listener) {
    assert(!poller_.get() && !listener_);
    poller_ = poller;
    listener_ = listener;
  }

  void changeListener(Ref<IOListener> listener);

 protected:
  WinContext *checkOp(IOResult *r, Ref<IOContext> context, size_t length);

 protected:
  TransportFlags flags_;
  Ref<IOListener> listener_;
  AtomicRef<WinBasePoller> poller_;
};

} // namespace amio

#endif // _include_amio_windows_transport_h_
