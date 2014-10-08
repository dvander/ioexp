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
#include "include/amio-posix.h"
#include "posix/amio-posix-transport.h"
#include "posix/amio-posix-base-poller.h"
#include <sys/time.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <am-utility.h>
#include <am-vector.h>

namespace amio {

using namespace ke;

static const size_t kDefaultMaxEventsPerPoll = 256;

// This message pump is based on epoll(), which is available in Linux >= 2.5.44.
class EpollImpl : public PosixPoller
{
 public:
  EpollImpl(size_t maxEvents = kDefaultMaxEventsPerPoll);
  ~EpollImpl();

  PassRef<IOError> Initialize();
  PassRef<IOError> Poll(int timeoutMs) override;
  PassRef<IOError> Attach(Ref<Transport> transport, Ref<StatusListener> listener, EventFlags eventMask) override;
  void Detach(Ref<Transport> baseTransport) override;
  void Interrupt() override;
  PassRef<IOError> ChangeStickyEvents(Ref<Transport> transport, EventFlags eventMask) override;

  PassRef<IOError> onReadWouldBlock(PosixTransport *transport) override;
  PassRef<IOError> onWriteWouldBlock(PosixTransport *transport) override;
  void unhook(PosixTransport *transport) override;

 private:
  bool isFdChanged(size_t slot) const {
    return listeners_[slot].modified == generation_;
  }
  
  template <EventFlags outFlag>
  inline void handleEvent(size_t slot);

 private:
  struct PollData {
    Ref<PosixTransport> transport;
    size_t modified;
    epoll_event pe;
    EventFlags flags;
  };

  int ep_;
  bool can_use_rdhup_;
  size_t generation_;

  // Note: we currently do not shrink slots.
  ke::Vector<PollData> listeners_;
  ke::Vector<size_t> free_slots_;

  size_t max_events_;
  ke::AutoArray<epoll_event> event_buffer_;
};

} // namespace amio

#endif // _include_amio_linux_epoll_pump_h_
