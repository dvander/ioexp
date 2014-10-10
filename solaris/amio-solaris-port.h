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
#include "include/amio-posix.h"
#include "posix/amio-posix-transport.h"
#include "posix/amio-posix-base-poller.h"
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
  PassRef<IOError> Attach(Ref<Transport> transport, Ref<StatusListener> listener, EventFlags eventMask) override;
  void Detach(Ref<Transport> baseTransport) override;
  void Interrupt() override;
  PassRef<IOError> ChangeStickyEvents(Ref<Transport> transport, EventFlags eventMask) override;

  PassRef<IOError> onReadWouldBlock(PosixTransport *transport) override;
  PassRef<IOError> onWriteWouldBlock(PosixTransport *transport) override;
  void unhook(PosixTransport *transport) override;

 private:
  bool isFdChanged(size_t slot) const {
    return fds_[slot].modified == generation_;
  }

  inline PassRef<IOError> addEventFlags(size_t slot, int flags);

  template <int inFlag>
  inline void handleEvent(size_t slot);

 private:
  struct PollData {
    Ref<PosixTransport> transport;
    volatile size_t modified;

    int events;
  };

  int port_;
  uintptr_t generation_;

  Vector<PollData> fds_;
  Vector<size_t> free_slots_;

  size_t max_events_;
  AutoPtr<port_event_t> event_buffer_;
};

} // namespace amio

#endif // _include_amio_solaris_port_pump_h_
