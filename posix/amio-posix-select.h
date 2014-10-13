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
#include "posix/amio-posix-base-poller.h"
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

  PassRef<IOError> Poll(int timeoutMs) override;
  void Interrupt() override;
  void Shutdown() override;

  PassRef<IOError> attach_locked(
    PosixTransport *transport,
    StatusListener *listener,
    TransportFlags flags) override;
  void detach_locked(PosixTransport *transport) override;
  PassRef<IOError> change_events_locked(PosixTransport *transport, TransportFlags flags) override;

 private:
  bool isFdChanged(int fd) const {
    return fds_[fd].modified == generation_;
  }

  void select_ctl(int fd, TransportFlags flags);

  template <TransportFlags outFlag>
  inline void handleEvent(fd_set *set, int fd);

 private:
  struct SelectData {
    Ref<PosixTransport> transport;
    uintptr_t modified;

    SelectData() : modified(0)
    {}
  };

  int fd_watermark_;
  fd_set read_fds_;
  fd_set write_fds_;
  size_t max_fds_;
  uintptr_t generation_;
  AutoArray<SelectData> fds_;
};

} // namespace amio

#endif // _include_amio_select_pump_h_
