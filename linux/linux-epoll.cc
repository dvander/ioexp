// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "posix/posix-errors.h"
#include "linux/linux-utils.h"
#include "linux/linux-epoll.h"
#include <unistd.h>
#include <sys/epoll.h>
#include <limits.h>

#if !defined(EPOLLRDHUP)
# define EPOLLRDHUP 0x2000
#endif

using namespace ke;
using namespace amio;

// This is passed to the kernel, which ignores it. But it has to be non-zero.
static const size_t kInitialEpollSize = 16;

EpollImpl::EpollImpl(size_t maxEvents)
 : ep_(-1),
   can_use_rdhup_(false),
   generation_(0),
   max_events_(0),
   absolute_max_events_(maxEvents)
{
#if defined(__linux__)
  if (IsAtLeastLinux(2, 6, 17))
    can_use_rdhup_ = true;
#endif
}

PassRef<IOError>
EpollImpl::Initialize()
{
  if ((ep_ = epoll_create(kInitialEpollSize)) == -1)
    return new PosixError();

  if (absolute_max_events_)
    max_events_ = absolute_max_events_;
  else
    max_events_ = 32;

  event_buffer_ = new epoll_event[max_events_];
  if (!event_buffer_)
    return eOutOfMemory;

  return nullptr;
}

EpollImpl::~EpollImpl()
{
  Shutdown();
}

void
EpollImpl::Shutdown()
{
  AutoMaybeLock lock(lock_);

  if (ep_ == -1)
    return;

  for (size_t i = 0; i < listeners_.length(); i++) {
    if (listeners_[i].transport)
      detach_for_shutdown_locked(listeners_[i].transport);
  }

  close(ep_);
  ep_ = -1;
}

PassRef<IOError>
EpollImpl::attach_locked(PosixTransport *transport, StatusListener *listener, TransportFlags flags)
{
  size_t slot;
  if (!free_slots_.empty()) {
    slot = free_slots_.popCopy();
  } else {
    slot = listeners_.length();
    if (!listeners_.append(PollData()))
      return eOutOfMemory;
  }

  // Hook up events.
  if (Ref<IOError> error = epoll_ctl(EPOLL_CTL_ADD, slot, transport->fd(), flags)) {
    free_slots_.append(slot);
    return error;
  }

  // Hook up the transport.
  listeners_[slot].transport = transport;
  listeners_[slot].modified = generation_;
  transport->attach(this, listener);
  transport->setUserData(slot);
  transport->flags() |= flags;
  return nullptr;
}

PassRef<StatusListener>
EpollImpl::detach_locked(PosixTransport *transport)
{
  int fd = transport->fd();
  size_t slot = transport->getUserData();
  assert(fd != -1);
  assert(listeners_[slot].transport == transport);

  epoll_event ep;
  ::epoll_ctl(ep_, EPOLL_CTL_DEL, fd, &ep);

  listeners_[slot].transport = nullptr;
  listeners_[slot].modified = generation_;
  free_slots_.append(slot);

  return transport->detach();
}

PassRef<IOError>
EpollImpl::epoll_ctl(int cmd, size_t slot, int fd, TransportFlags flags)
{
  epoll_event pe;
  pe.data.ptr = (void *)slot;
  pe.events = (flags & kTransportET) ? EPOLLET : 0;
  if (can_use_rdhup_)
    pe.events |= EPOLLRDHUP;
  if (flags & kTransportReading)
    pe.events |= EPOLLIN;
  if (flags & kTransportWriting)
    pe.events |= EPOLLOUT;

  if (::epoll_ctl(ep_, cmd, fd, &pe) == -1)
    return new PosixError();

  return nullptr;
}

PassRef<IOError>
EpollImpl::change_events_locked(PosixTransport *transport, TransportFlags flags)
{
  size_t slot = transport->getUserData();

  if (Ref<IOError> error = epoll_ctl(EPOLL_CTL_MOD, slot, transport->fd(), flags))
    return error;

  transport->flags() &= ~kTransportEventMask;
  transport->flags() |= flags;
  return nullptr;
}

template <TransportFlags outFlag>
inline void
EpollImpl::handleEvent(size_t slot)
{
  Ref<PosixTransport> transport = listeners_[slot].transport;

  // If we are listening for sticky events, but not this event, bail out. We
  // only check this for sticky events, since edge-triggered transports cannot
  // be changed.
  if ((transport->flags() & (outFlag|kTransportLT)) == kTransportLT)
    return;

  // We must hold the listener in a ref, since if the transport is detached
  // in the callback, it could be destroyed while |this| is still on the
  // stack. Similarly, we must hold the transport in a ref in case releasing
  // the lock allows a detach to happen.
  Ref<StatusListener> listener = transport->listener();

  AutoMaybeUnlock unlock(lock_);
  if (outFlag == kTransportReading)
    listener->OnReadReady();
  else if (outFlag == kTransportWriting)
    listener->OnWriteReady();
}

PassRef<IOError>
EpollImpl::Poll(int timeoutMs)
{
  // We acquire a lock specifically for Poll(), to make sure it isn't called on
  // any thread or within callbacks.
  AutoMaybeLock poll_lock(poll_lock_);

  int nevents = epoll_wait(ep_, event_buffer_, max_events_, timeoutMs);
  if (nevents == -1)
    return new PosixError();

  // Now we acquire the transport lock.
  AutoMaybeLock lock(lock_);

  generation_++;
  for (int i = 0; i < nevents; i++) {
    epoll_event &ep = event_buffer_[i];
    size_t slot = (size_t)ep.data.ptr;
    if (isFdChanged(slot))
      continue;

    // Handle errors first.
    if (ep.events & EPOLLERR) {
      reportError_locked(listeners_[slot].transport);
      continue;
    }

    // Prioritize EPOLLIN over EPOLLHUP/EPOLLRDHUP.
    if (ep.events & EPOLLIN) {
      handleEvent<kTransportReading>(slot);
      if (isFdChanged(slot))
        continue;
    }

    // Handle explicit hangup.
    if (ep.events & (EPOLLRDHUP|EPOLLHUP)) {
      reportHup_locked(listeners_[slot].transport);
      continue;
    }

    // Handle output.
    if (ep.events & EPOLLOUT)
      handleEvent<kTransportWriting>(slot);
  }

  // If we filled the event buffer, resize it for next time.
  if (!absolute_max_events_ && size_t(nevents) == max_events_ && max_events_ < (INT_MAX / 2)) {
    AutoMaybeUnlock unlock(lock_);

    size_t new_size = max_events_ * 2;
    epoll_event *new_buffer = new epoll_event[new_size];
    if (new_buffer) {
      max_events_ = new_size;
      event_buffer_ = new_buffer;
    }
  }

  return nullptr;
}
