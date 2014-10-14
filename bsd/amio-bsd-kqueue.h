// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_bsd_kqueue_pump_h_
#define _include_amio_bsd_kqueue_pump_h_

#include "include/amio.h"
#include "include/amio-posix.h"
#include "posix/amio-posix-base-poller.h"
#include <sys/types.h>
#include <sys/event.h>
#include <am-utility.h>
#include <am-vector.h>

namespace amio {

using namespace ke;

class KqueueImpl : public PosixPoller
{
 public:
  KqueueImpl(size_t maxEvents = 0);
  ~KqueueImpl();

  PassRef<IOError> Initialize();
  PassRef<IOError> Poll(int timeoutMs) override;
  void Interrupt() override;
  void Shutdown() override;
  bool SupportsEdgeTriggering() override {
    return true;
  }

  PassRef<IOError> attach_locked(
    PosixTransport *transport,
    StatusListener *listener,
    TransportFlags flags) override;
  void detach_locked(PosixTransport *transport) override;
  PassRef<IOError> change_events_locked(PosixTransport *transport, TransportFlags flags) override;

 private:
  bool isFdChanged(size_t slot) const {
    return listeners_[slot].modified == generation_;
  }

 private:
  struct PollData {
    Ref<PosixTransport> transport;
    size_t modified;
  };

  int kq_;
  size_t generation_;
  ke::Vector<PollData> listeners_;
  ke::Vector<size_t> free_slots_;

  size_t max_events_;
  size_t absolute_max_events_;
  ke::AutoArray<struct kevent> event_buffer_;
};

} // namespace amio

#endif // _include_amio_bsd_kqueue_pump_h_
