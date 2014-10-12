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
#include "posix/amio-posix-transport.h"
#include "posix/amio-posix-base-poller.h"
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
class PollImpl : public PosixPoller
{
 public:
  PollImpl();
  ~PollImpl();

  PassRef<IOError> Initialize();
  PassRef<IOError> Poll(int timeoutMs) override;
  void Interrupt() override;
  void Shutdown() override;
  bool SupportsEdgeTriggering() override {
    return false;
  }
  size_t MaximumConcurrency() override {
    return 0;
  }

  PassRef<IOError> attach_locked(
    PosixTransport *transport,
    StatusListener *listener,
    TransportFlags flags) override;
  void detach_locked(PosixTransport *transport) override;
  PassRef<IOError> change_events_locked(PosixTransport *transport, TransportFlags flags) override;

 private:
  void poll_ctl(size_t slot, TransportFlags flags);

  bool isFdChanged(int fd) const {
    return fds_[fd].modified == generation_;
  }

  template <int inFlag, TransportFlags outFlag>
  inline void handleEvent(size_t event_idx, int fd);

 private:
  struct PollData {
    Ref<PosixTransport> transport;
    uintptr_t modified;

    PollData() : modified(0)
    {}
  };

#if defined(__linux__)
  bool can_use_rdhup_;
#endif
  uintptr_t generation_;
  Vector<struct pollfd> poll_events_;
  Vector<PollData> fds_;
  Vector<size_t> free_slots_;
  AutoPtr<struct pollfd> tmp_buffer_;
  size_t tmp_buffer_len_;
};

} // namespace amio

#endif // _include_amio_poll_pump_h_

