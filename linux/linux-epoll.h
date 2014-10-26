// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_linux_epoll_pump_h_
#define _include_amio_linux_epoll_pump_h_

#include "include/amio.h"
#include "posix/posix-transport.h"
#include "posix/posix-base-poller.h"
#include <sys/time.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <am-utility.h>
#include <am-vector.h>

namespace amio {

using namespace ke;

// This message pump is based on epoll(), which is available in Linux >= 2.5.44.
class EpollImpl : public PosixPoller
{
 public:
  EpollImpl(size_t maxEvents = 0);
  ~EpollImpl();

  PassRef<IOError> Initialize();
  PassRef<IOError> Poll(int timeoutMs) override;
  void Shutdown() override;
  bool SupportsEdgeTriggering() override {
    return true;
  }

  PassRef<IOError> attach_locked(
    PosixTransport *transport,
    StatusListener *listener,
    TransportFlags flags) override;
  PassRef<StatusListener> detach_locked(PosixTransport *transport) override;
  PassRef<IOError> change_events_locked(PosixTransport *transport, TransportFlags flags) override;

 private:
  bool isFdChanged(size_t slot) const {
    return listeners_[slot].modified == generation_;
  }

  PassRef<IOError> epoll_ctl(int cmd, size_t slot, int fd, TransportFlags);
  
  template <TransportFlags outFlag>
  inline void handleEvent(size_t slot);

 private:
  struct PollData {
    Ref<PosixTransport> transport;
    size_t modified;
  };

  int ep_;
  bool can_use_rdhup_;
  size_t generation_;

  // Note: we currently do not shrink slots.
  ke::Vector<PollData> listeners_;
  ke::Vector<size_t> free_slots_;

  size_t max_events_;
  size_t absolute_max_events_;
  ke::AutoArray<epoll_event> event_buffer_;
};

} // namespace amio

#endif // _include_amio_linux_epoll_pump_h_
