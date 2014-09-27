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
#include <sys/event.h>
#include <sys/types.h>
#include <am-utility.h>
#include <am-vector.h>

namespace amio {

using namespace ke;

static const size_t kDefaultMaxEventsPerPoll = 256;

class KqueueImpl : public PosixPoller
{
 public:
  KqueueImpl(size_t maxEvents = kDefaultMaxEventsPerPoll);
  ~KqueueImpl();

  PassRef<IOError> Initialize();
  PassRef<IOError> Poll(int timeoutMs) override;
  PassRef<IOError> Register(Ref<Transport> transport, Ref<StatusListener> listener) override;
  void Deregister(Ref<Transport> baseTransport) override;
  void Interrupt() override;

  void onReadWouldBlock(PosixTransport *transport) override;
  PassRef<IOError> onWriteWouldBlock(PosixTransport *transport) override;
  void unhook(ke::Ref<PosixTransport> transport) override;

 private:
  bool isEventValid(size_t slot) const {
    return slot < listeners_.length() &&
           listeners_[slot].modified != generation_;
  }

 private:
  struct PollData {
    Ref<PosixTransport> transport;
    size_t modified;
    bool watching_writes;
  };

  int kq_;
  size_t generation_;
  ke::Vector<PollData> listeners_;
  ke::Vector<size_t> free_slots_;

  size_t max_events_;
  ke::AutoArray<struct kevent> event_buffer_;
};

} // namespace amio

#endif // _include_amio_bsd_kqueue_pump_h_
