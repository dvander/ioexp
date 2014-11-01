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

#include <amio.h>
#include "windows-transport.h"

namespace amio {

using namespace ke;

class WinBasePoller;
class WinBaseSocketPoller;

class SocketTransport : public WinTransport
{
 public:
  SocketTransport(SOCKET socket, TransportFlags flags);
  ~SocketTransport();

  bool read(IOResult *r, WinBasePoller *poller, WinContext *context, void *buffer, size_t length) override;
  bool write(IOResult *r, WinBasePoller *poller, WinContext *context, const void *buffer, size_t length) override;

  void Close() override;
  bool Closed() override {
    return socket_ == INVALID_SOCKET;
  }
  virtual HANDLE Handle() override {
    return (HANDLE)socket_;
  }
  virtual SOCKET socket() const {
    return socket_;
  }

  virtual PassRef<IOError> EnableImmediateDelivery() override;
  virtual DWORD GetOverlappedError(OVERLAPPED *ovp) override;

 protected:
  SOCKET socket_;
};

} // namespace amio

#endif // _include_amio_windows_socket_h_
