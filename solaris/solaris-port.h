// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_solaris_port_pump_h_
#define _include_amio_solaris_port_pump_h_

#include "include/amio.h"
#include "shared/shared-pollbuf.h"
#include "posix/posix-transport.h"
#include "posix/posix-base-poller.h"
#include <sys/time.h>
#include <sys/types.h>
#include <port.h>
#include <am-utility.h>
#include <am-vector.h>
#include <am-thread-utils.h>

namespace amio {

using namespace ke;

static const size_t kDefaultMaxEventsPerPortPoll = 256;

// This message pump is based on epoll(), which is available in Linux >= 2.5.44.
class PortImpl : public PosixPoller
{
 public:
  PortImpl();
  ~PortImpl();

  PassRef<IOError> Initialize(size_t maxEventsPerPoll = 0);
  PassRef<IOError> Poll(int timeoutMs) override;
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
  PassRef<StatusListener> detach_locked(PosixTransport *transport) override;
  PassRef<IOError> change_events_locked(PosixTransport *transport, TransportFlags flags) override;

 private:
  bool isFdChanged(size_t slot) const {
    return fds_[slot].modified == generation_;
  }

  inline PassRef<IOError> addEventFlags(size_t slot, int flags);

  template <TransportFlags outFlag>
  inline void handleEvent(size_t slot);

 private:
  struct PollData {
    Ref<PosixTransport> transport;
    size_t modified;
  };

  int port_;
  uintptr_t generation_;

  Vector<PollData> fds_;
  Vector<size_t> free_slots_;

  MultiPollBuffer<port_event_t> event_buffers_;
};

} // namespace amio

#endif // _include_amio_solaris_port_pump_h_
