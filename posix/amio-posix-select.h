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
#include "posix/amio-posix-transport.h"
#include "posix/amio-posix-base-pump.h"
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <am-utility.h>

namespace amio {

using namespace ke;

class SelectImpl : public PosixPoller
{
 public:
  SelectImpl();
  ~SelectImpl();

  Ref<IOError> Poll(int timeoutMs) override;
  Ref<IOError> Register(Ref<Transport> transport, Ref<StatusListener> listener) override;
  void Deregister(Ref<Transport> baseTransport) override;
  void Interrupt() override;

  void onReadWouldBlock(PosixTransport *transport) override;
  PassRef<IOError> onWriteWouldBlock(PosixTransport *transport) override;
  void unhook(Ref<PosixTransport> transport) override;

 private:
  bool isEventValid(size_t slot) const {
    return listeners_[slot].modified != generation_;
  }

 private:
  struct SelectData {
    Ref<PosixTransport> transport;
    Ref<StatusListener> listener;
    uintptr_t modified;

    SelectData() : modified(0)
    {}
  };

  int fd_watermark_;
  fd_set read_fds_;
  fd_set write_fds_;
  size_t max_listeners_;
  uintptr_t generation_;
  AutoArray<SelectData> listeners_;
};

} // namespace amio

#endif // _include_amio_select_pump_h_
