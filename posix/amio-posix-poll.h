// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_poll_pump_h_
#define _include_amio_poll_pump_h_

#include "include/amio.h"
#include "include/amio-posix-transport.h"
#include "posix/amio-posix-base-pump.h"
#include <sys/time.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <am-utility.h>
#include <am-vector.h>

namespace amio {

using namespace ke;

// This message pump is based on poll(), which is available in glibc, Linux,
// and BSD. Notably it is not present (as a function call) on Solaris, but
// as a device (/dev/poll) which deserves a separate implementation.
class PollMessagePump : public PosixPump
{
 public:
  PollMessagePump();
  ~PollMessagePump();

  Ref<IOError> Initialize() override;
  Ref<IOError> Poll(int timeoutMs) override;
  Ref<IOError> Register(Ref<Transport> transport, Ref<StatusListener> listener) override;
  void Deregister(Ref<Transport> baseTransport) override;
  void Interrupt() override;

  void onReadWouldBlock(int fd) override;
  void onWriteWouldBlock(int fd) override;
  void onClose(int fd) override;

 private:
  bool handleRead(int fd);
  bool handleWrite(int fd);

 private:
  struct PollData {
    Ref<PosixTransport> transport;
    Ref<StatusListener> listener;
    size_t slot;
  };

  bool can_use_rdhup_;
  ke::Vector<struct pollfd> pollfds_;
  ke::Vector<PollData> listeners_;
  ke::Vector<size_t> free_slots_;
};

} // namespace amio

#endif // _include_amio_poll_pump_h_

