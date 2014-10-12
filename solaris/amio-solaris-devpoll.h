// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_solaris_devpoll_pump_h_
#define _include_amio_solaris_devpoll_pump_h_

#include "include/amio.h"
#include "include/amio-posix.h"
#include "shared/amio-shared-pollbuf.h"
#include "posix/amio-posix-transport.h"
#include "posix/amio-posix-base-poller.h"
#include <sys/time.h>
#include <sys/types.h>
#include <poll.h>
#include <am-utility.h>
#include <am-vector.h>

namespace amio {

using namespace ke;

// This message pump is based on epoll(), which is available in Linux >= 2.5.44.
class DevPollImpl : public PosixPoller
{
 public:
  DevPollImpl();
  ~DevPollImpl();

  PassRef<IOError> Initialize(size_t maxEventsPerPoll = 0);
  PassRef<IOError> Poll(int timeoutMs) override;
  void Interrupt() override;
  void Shutdown() override;
  bool SupportsEdgeTriggering() override {
    return false;
  }

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

  template <int inFlag, TransportFlags outFlag>
  inline void handleEvent(int fd);

 private:
  struct PollData {
    Ref<PosixTransport> transport;
    size_t modified;
  };

  int dp_;
  size_t generation_;
  ke::Vector<PollData> fds_;

  PollBuffer<struct pollfd> event_buffer_;
};

} // namespace amio

#endif // _include_amio_solaris_devpoll_pump_h_


