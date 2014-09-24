// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_select_pump_h_
#define _include_amio_select_pump_h_

#include "include/amio.h"
#include "include/amio-posix-transport.h"
#include "posix/amio-posix-base-pump.h"
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <am-utility.h>

namespace amio {

using namespace ke;

class SelectMessagePump : public PosixPump
{
 public:
  SelectMessagePump();
  ~SelectMessagePump();

  bool Poll(struct timeval *timeoutp);
  Ref<IOError> Register(
    Ref<Transport> transport, 
    Ref<StatusListener> listener
  );
  void Deregister(Ref<Transport> baseTransport);

  void onReadWouldBlock(int fd) override;
  void onWriteWouldBlock(int fd) override;
  void onClose(int fd) override;

 private:
  bool handleRead(int fd);
  bool handleWrite(int fd);

 private:
  struct SelectData {
    Ref<PosixTransport> transport;
    Ref<StatusListener> listener;
  };

  int fd_watermark_;
  fd_set read_fds_;
  fd_set write_fds_;
  size_t max_listeners_;
  AutoArray<SelectData> listeners_;
};

} // namespace amio

#endif // _include_amio_select_pump_h_
