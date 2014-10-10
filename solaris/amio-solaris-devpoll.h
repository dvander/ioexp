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
#include "posix/amio-posix-transport.h"
#include "posix/amio-posix-base-poller.h"
#include <sys/time.h>
#include <sys/types.h>
#include <poll.h>
#include <am-utility.h>
#include <am-vector.h>

namespace amio {

using namespace ke;

static const size_t kDefaultMaxEventsPerPoll = 256;

// This message pump is based on epoll(), which is available in Linux >= 2.5.44.
class DevPollImpl : public PosixPoller
{
 public:
  DevPollImpl();
  ~DevPollImpl();

  PassRef<IOError> Initialize(size_t maxEventsPerPoll = 0);
  PassRef<IOError> Poll(int timeoutMs) override;
  PassRef<IOError> Attach(Ref<Transport> transport, Ref<StatusListener> listener, EventFlags eventMask) override;
  void Detach(Ref<Transport> baseTransport) override;
  void Interrupt() override;
  PassRef<IOError> ChangeStickyEvents(Ref<Transport> transport, EventFlags eventMask) override;

  PassRef<IOError> addEventFlag(int fd, int flag);
  PassRef<IOError> onReadWouldBlock(PosixTransport *transport) override;
  PassRef<IOError> onWriteWouldBlock(PosixTransport *transport) override;
  void unhook(PosixTransport *transport) override;

 private:
  bool isFdChanged(int fd) const {
    return fds_[fd].modified == generation_;
  }
  template <int inFlag>
  inline PassRef<IOError> addEventFlag(PosixTransport *transport);
  template <int inFlag>
  inline void handleEvent(int fd);

 private:
  struct PollData {
    Ref<PosixTransport> transport;
    size_t modified;
    struct pollfd pe;

    // devpoll doesn't have edge triggering, and modifying it is kind of
    // annoying, so instead we flag POLLIN/POLLOUT here. We also stick a
    // custom flag here to indicate level-triggering.
    int flags;
  };

  int dp_;
  size_t generation_;
  ke::Vector<PollData> fds_;

  size_t max_events_;
  ke::AutoArray<struct pollfd> event_buffer_;
};

} // namespace amio

#endif // _include_amio_solaris_devpoll_pump_h_


