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

#include <amio-windows.h>

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

  // This is only called if the poller has determined ahead of time that
  // immediate delivery can be used.
  virtual PassRef<IOError> EnableImmediateDelivery() = 0;

  // Get the last error in the context of the transport type.
  virtual int LastError() = 0;

  WinContext *checkOp(IOResult *r, Ref<IOContext> context, size_t length);

  PassRef<IOListener> listener() const {
    return listener_;
  }
  PassRef<WinBasePoller> poller() const {
    return poller_;
  }
  bool ImmediateDelivery() const {
    return !!(flags_ & kTransportImmediateDelivery);
  }
  void attach(PassRef<WinBasePoller> poller, PassRef<IOListener> listener) {
    assert(!poller_ && !listener_);
    poller_ = poller;
    listener_ = listener;
  }
  void changeListener(Ref<IOListener> listener) {
    listener_ = listener;
  }

 protected:
  TransportFlags flags_;
  Ref<IOListener> listener_;
  Ref<WinBasePoller> poller_;
};

} // namespace amio

#endif // _include_amio_windows_transport_h_
