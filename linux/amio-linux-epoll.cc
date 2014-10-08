// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "posix/amio-posix-errors.h"
#include "linux/amio-linux.h"
#include "linux/amio-linux-epoll.h"
#include <unistd.h>
#include <sys/epoll.h>

#if !defined(EPOLLRDHUP)
# define EPOLLRDHUP 0x2000
#endif

static const uint32_t kEmulateReadET  = 0x1;
static const uint32_t kEmulateWriteET = 0x2;
static const uint32_t kReadStickied   = 0x4;
static const uint32_t kWriteStickied  = 0x8;

using namespace ke;
using namespace amio;

// This is passed to the kernel, which ignores it. But it has to be non-zero.
static const size_t kInitialEpollSize = 16;

EpollImpl::EpollImpl(size_t maxEvents)
 : ep_(-1),
   can_use_rdhup_(false),
   generation_(0),
   max_events_(maxEvents ? maxEvents : kDefaultMaxEventsPerPoll)
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

  event_buffer_ = new epoll_event[max_events_];
  if (!event_buffer_)
    return eOutOfMemory;

  return nullptr;
}

EpollImpl::~EpollImpl()
{
  if (ep_ == -1)
    return;

  for (size_t i = 0; i < listeners_.length(); i++) {
    if (listeners_[i].transport)
      listeners_[i].transport->detach();
  }

  close(ep_);
}

PassRef<IOError>
EpollImpl::Attach(Ref<Transport> baseTransport, Ref<StatusListener> listener, EventFlags eventMask)
{
  PosixTransport *transport;
  Ref<IOError> error = toPosixTransport(&transport, baseTransport);
  if (error)
    return error;

  assert(listener);

  size_t slot;
  if (!free_slots_.empty()) {
    slot = free_slots_.popCopy();
  } else {
    slot = listeners_.length();
    if (!listeners_.append(PollData()))
      return eOutOfMemory;
  }

  // Hook up events.
  epoll_event pe;
  pe.data.ptr = (void *)slot;
  pe.events = (eventMask & Event_Sticky) ? 0 : EPOLLET;
  if (can_use_rdhup_)
    pe.events |= EPOLLRDHUP;
  if (eventMask & Event_Read)
    pe.events |= EPOLLIN;
  if (eventMask & Event_Write)
    pe.events |= EPOLLOUT;

  if (epoll_ctl(ep_, EPOLL_CTL_ADD, transport->fd(), &pe) == -1) {
    Ref<IOError> err = new PosixError();
    free_slots_.append(slot);
    return err;
  }

  // Hook up the transport.
  listeners_[slot].transport = transport;
  listeners_[slot].modified = generation_;
  listeners_[slot].pe = pe;
  listeners_[slot].flags = eventMask;
  transport->attach(this, listener);
  transport->setUserData(slot);
  return nullptr;
}

PassRef<IOError>
EpollImpl::ChangeStickyEvents(Ref<Transport> baseTransport, EventFlags eventMask)
{
  Ref<PosixTransport> transport = validateEventChange(baseTransport, eventMask);
  if (!transport)
    return eIncompatibleTransport;

  size_t slot = transport->getUserData();
  if (!(listeners_[slot].flags & Event_Sticky))
    return eIncompatibleTransport;
  if (listeners_[slot].flags == eventMask)
    return nullptr;

  epoll_event pe = listeners_[slot].pe;
  pe.events &= ~(EPOLLIN|EPOLLOUT);
  if (eventMask & Event_Read)
    pe.events |= EPOLLIN;
  if (eventMask & Event_Write)
    pe.events |= EPOLLOUT;
  if (epoll_ctl(ep_, EPOLL_CTL_MOD, transport->fd(), &pe) == -1)
    return new PosixError();

  listeners_[slot].flags = eventMask;
  listeners_[slot].pe.events = pe.events;
  return nullptr;
}

void
EpollImpl::Detach(Ref<Transport> baseTransport)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport || transport->pump() != this || transport->fd() == -1)
    return;

  unhook(transport);
}

template <EventFlags outFlag>
inline void
EpollImpl::handleEvent(size_t slot)
{
  // If we are listening for sticky events, but not this event, bail out. We
  // only check this for sticky events, since edge-triggered transports cannot
  // be changed.
  if ((listeners_[slot].flags & (outFlag|Event_Sticky)) == Event_Sticky)
    return;

  if (outFlag == Event_Read)
    listeners_[slot].transport->listener()->OnReadReady(listeners_[slot].transport);
  else if (outFlag == Event_Write)
    listeners_[slot].transport->listener()->OnWriteReady(listeners_[slot].transport);
}

PassRef<IOError>
EpollImpl::Poll(int timeoutMs)
{
  int nevents = epoll_wait(ep_, event_buffer_, max_events_, timeoutMs);
  if (nevents == -1)
    return new PosixError();

  generation_++;
  for (int i = 0; i < nevents; i++) {
    epoll_event &ep = event_buffer_[i];
    size_t slot = (size_t)ep.data.ptr;
    if (isFdChanged(slot))
      continue;

    // Handle errors first.
    if (ep.events & EPOLLERR) {
      reportError(listeners_[slot].transport);
      continue;
    }

    // Prioritize EPOLLIN over EPOLLHUP/EPOLLRDHUP.
    if (ep.events & EPOLLIN) {
      handleEvent<Event_Read>(slot);
      if (isFdChanged(slot))
        continue;
    }

    // Handle explicit hangup.
    if (ep.events & (EPOLLRDHUP|EPOLLHUP)) {
      reportHup(listeners_[slot].transport);
      continue;
    }

    // Handle output.
    if (ep.events & EPOLLOUT)
      handleEvent<Event_Write>(slot);
  }

  return nullptr;
}

void
EpollImpl::Interrupt()
{
  // Not yet implemented.
  abort();
}

PassRef<IOError>
EpollImpl::onReadWouldBlock(PosixTransport *transport)
{
  size_t slot = transport->getUserData();
  if (!(listeners_[slot].pe.events & EPOLLIN)) {
    listeners_[slot].pe.events |= EPOLLIN;
    if (epoll_ctl(ep_, EPOLL_CTL_MOD, transport->fd(), &listeners_[slot].pe) == -1) {
      listeners_[slot].pe.events &= ~EPOLLIN;
      return new PosixError();
    }
  }
  return nullptr;
}

PassRef<IOError>
EpollImpl::onWriteWouldBlock(PosixTransport *transport)
{
  size_t slot = transport->getUserData();
  if (!(listeners_[slot].pe.events & EPOLLOUT)) {
    listeners_[slot].pe.events |= EPOLLOUT;
    if (epoll_ctl(ep_, EPOLL_CTL_MOD, transport->fd(), &listeners_[slot].pe) == -1) {
      listeners_[slot].pe.events &= ~EPOLLOUT;
      return new PosixError();
    }
  }
  return nullptr;
}

void
EpollImpl::unhook(PosixTransport *transport)
{
  assert(transport->pump() == this);

  int fd = transport->fd();
  size_t slot = transport->getUserData();
  assert(fd != -1);
  assert(listeners_[slot].transport == transport);

  epoll_ctl(ep_, EPOLL_CTL_DEL, fd, &listeners_[slot].pe);

  // Just for safety, we detach here in case the assignment below drops the
  // last ref to the transport.
  transport->detach();

  listeners_[slot].transport = nullptr;
  listeners_[slot].modified = generation_;
  free_slots_.append(slot);
}
