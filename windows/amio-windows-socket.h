// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
#ifndef _include_amio_windows_socket_h_
#define _include_amio_windows_socket_h_

#include <amio-windows.h>
#include "amio-windows-transport.h"

namespace amio {

using namespace ke;

class WinBasePoller;
class WinBaseSocketPoller;

class SocketTransport : public WinTransport
{
 public:
  SocketTransport(SOCKET socket, TransportFlags flags);
  ~SocketTransport();

  bool Read(IOResult *r, Ref<IOContext> context, void *buffer, size_t length) override;
  bool Write(IOResult *r, Ref<IOContext> context, const void *buffer, size_t length) override;

  void Close() override;
  bool Closed() override {
    return socket_ == INVALID_SOCKET;
  }
  virtual HANDLE Handle() override {
    return (HANDLE)socket_;
  }

  virtual PassRef<IOError> EnableImmediateDelivery();

 protected:
  SOCKET socket_;
};

class WinSocket : public Socket
{
 public:
  WinSocket(SOCKET s, SocketFlags flags);
  ~WinSocket();

  static PassRef<IOError> CreateFrom(Ref<Socket> *outp, SOCKET s, SocketFlags flagS);

  bool Read(IOResult *result, void *buffer, size_t maxlength) override;
  bool Write(IOResult *result, const void *buffer, size_t maxlength) override;
  void Close() override;
  bool Closed() const override {
    return socket_ == INVALID_SOCKET;
  }
  SOCKET Handle() const override {
    return socket_;
  }
  WinSocket *toWinSocket() override {
    return this;
  }
  WinBaseSocketPoller *poller() const {
    return poller_;
  }
  PassRef<SocketListener> listener() {
    return listener_;
  }
  void attach(WinBaseSocketPoller *poller, Ref<SocketListener> listener) {
    poller_ = poller;
    listener_ = listener;
  }
  void detach() {
    poller_ = nullptr;
    listener_ = nullptr;
  }

  void setUserData(uintptr_t user_data) {
    user_data_ = user_data;
  }
  uintptr_t userData() const {
    return user_data_;
  }

 private:
  SOCKET socket_;
  SocketFlags flags_;
  uintptr_t user_data_;
  WinBaseSocketPoller *poller_;
  Ref<SocketListener> listener_;
};

} // namespace amio

#endif // _include_amio_windows_socket_h_