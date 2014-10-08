// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "shared/amio-errors.h"
#include "posix/amio-posix-errors.h"
#include "bsd/amio-bsd-kqueue.h"
#include <unistd.h>
#include <sys/time.h>

using namespace ke;
using namespace amio;

#if defined(__NetBSD__)
typedef intptr_t kev_userdata_t;
#else
typedef void* kev_userdata_t;
#endif

KqueueImpl::KqueueImpl(size_t maxEvents)
 : kq_(-1),
   generation_(0),
   max_events_(maxEvents ? maxEvents : kDefaultMaxEventsPerPoll)
{
}

KqueueImpl::~KqueueImpl()
{
  if (kq_ == -1)
    return;

  for (size_t i = 0; i < listeners_.length(); i++) {
    if (listeners_[i].transport)
      listeners_[i].transport->detach();
  }

  close(kq_);
}

PassRef<IOError>
KqueueImpl::Initialize()
{
  if ((kq_ = kqueue()) == -1)
    return new PosixError();

  event_buffer_ = new struct kevent[max_events_];
  if (!event_buffer_)
    return eOutOfMemory;

  return nullptr;
}

PassRef<IOError>
KqueueImpl::Attach(Ref<Transport> baseTransport, Ref<StatusListener> listener, EventFlags eventMask)
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

  int baseFlags = EV_ADD;
  if (!(eventMask & Event_Sticky))
    baseFlags |= EV_CLEAR;

  int readFlags = baseFlags | ((eventMask & Event_Read) ? EV_ENABLE : EV_DISABLE);
  int writeFlags = baseFlags | ((eventMask & Event_Write) ? EV_ENABLE : EV_DISABLE);

  // Pre-fill the two kevent structs beforehand.
  EV_SET(&listeners_[slot].read, transport->fd(), EVFILT_READ, readFlags, 0, 0, kev_userdata_t(slot));
  EV_SET(&listeners_[slot].write, transport->fd(), EVFILT_WRITE, writeFlags, 0, 0, kev_userdata_t(slot));

  struct kevent events[2] = {
    listeners_[slot].read,
    listeners_[slot].write,
  };

  if (kevent(kq_, events, 2, nullptr, 0, nullptr) == -1) {
    Ref<IOError> error = new PosixError();
    free_slots_.append(slot);
    return error;
  }

  // Hook up the transport.
  listeners_[slot].transport = transport;
  listeners_[slot].modified = generation_;
  transport->attach(this, listener);
  transport->setUserData(slot);
  return nullptr;
}

template <int Remove, int Add>
static inline PassRef<IOError>
ToggleEvent(int kq, struct kevent *ev)
{
  ev->flags &= ~Remove;
  ev->flags |= Add;
  if (kevent(kq, ev, 1, nullptr, 0, nullptr) == -1) {
    ev->flags &= ~Add;
    ev->flags |= Remove;
    return new PosixError();
  }
  return nullptr;
}

PassRef<IOError>
KqueueImpl::ChangeStickyEvents(Ref<Transport> baseTransport, EventFlags eventMask)
{
  Ref<PosixTransport> transport = validateEventChange(baseTransport, eventMask);
  if (!transport)
    return eIncompatibleTransport;

  size_t slot = transport->getUserData();
  if (listeners_[slot].read.flags & EV_CLEAR)
    return eIncompatibleTransport;

  bool reading = !!(eventMask & Event_Read);
  bool writing = !!(eventMask & Event_Write);
  struct kevent &read = listeners_[slot].read;
  struct kevent &write = listeners_[slot].write;

  Ref<IOError> error;
  if (reading && (read.flags & EV_DISABLE))
    error = ToggleEvent<EV_DISABLE, EV_ENABLE>(kq_, &read);
  else if (!reading && !(read.flags & EV_DISABLE))
    error = ToggleEvent<EV_ENABLE, EV_DISABLE>(kq_, &read);
  if (error)
    return error;

  if (writing && (write.flags & EV_DISABLE))
    error = ToggleEvent<EV_DISABLE, EV_ENABLE>(kq_, &write);
  else if (!writing && !(write.flags & EV_DISABLE))
    error = ToggleEvent<EV_ENABLE, EV_DISABLE>(kq_, &write);
  return error;
}

void
KqueueImpl::Detach(Ref<Transport> baseTransport)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport || transport->pump() != this || transport->fd() == -1)
    return;

  unhook(transport);
}

PassRef<IOError>
KqueueImpl::Poll(int timeoutMs)
{
  struct timespec timeout;
  struct timespec *timeoutp = nullptr;
  if (timeoutMs >= 0) {
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_nsec = (timeoutMs % 1000) * 1000000;
    timeoutp = &timeout;
  }

  int nevents = kevent(kq_, nullptr, 0, event_buffer_, max_events_, timeoutp);
  if (nevents == -1)
    return new PosixError();

  generation_++;
  for (int i = 0; i < nevents; i++) {
    struct kevent &ev = event_buffer_[i];
    size_t slot = (size_t)ev.udata;
    if (isFdChanged(slot))
      continue;

    // Note: we check EV_DISABLED in case a level-triggered event set has
    // changed while processing events.
    PollData &data = listeners_[slot];
    if (ev.flags & EV_EOF) {
      reportHup(data.transport);
      continue;
    }

    switch (ev.filter) {
      case EVFILT_READ:
        if (data.read.flags & EV_DISABLE)
          continue;
        data.transport->listener()->OnReadReady(data.transport);
        break;

      case EVFILT_WRITE:
        if (data.write.flags & EV_DISABLE)
          continue;
        data.transport->listener()->OnWriteReady(data.transport);
        break;

      default:
        assert(false);
    }
  }

  return nullptr;
}

void
KqueueImpl::Interrupt()
{
  // Not yet implemented.
  abort();
}

PassRef<IOError>
KqueueImpl::onReadWouldBlock(PosixTransport *transport)
{
  size_t slot = transport->getUserData();
  assert(listeners_[slot].transport == transport);
  if (listeners_[slot].read.flags & EV_DISABLE)
    return ToggleEvent<EV_DISABLE, EV_ENABLE>(kq_, &listeners_[slot].read);
  return nullptr;
}

PassRef<IOError>
KqueueImpl::onWriteWouldBlock(PosixTransport *transport)
{
  size_t slot = transport->getUserData();
  assert(listeners_[slot].transport == transport);
  if (listeners_[slot].write.flags & EV_DISABLE)
    return ToggleEvent<EV_DISABLE, EV_ENABLE>(kq_, &listeners_[slot].write);
  return nullptr;
}

void
KqueueImpl::unhook(PosixTransport *transport)
{
  assert(transport->pump() == this);

  int fd = transport->fd();
  uintptr_t slot = transport->getUserData();
  assert(fd != -1);
  assert(listeners_[slot].transport == transport);

  listeners_[slot].read.flags = EV_DELETE;
  listeners_[slot].write.flags = EV_DELETE;

  struct kevent events[2] = {
    listeners_[slot].read,
    listeners_[slot].write,
  };
  kevent(kq_, events, 2, nullptr, 0, nullptr);

  // Detach here, just for safety - in case null below frees it.
  transport->detach();

  listeners_[slot].transport = nullptr;
  listeners_[slot].modified = generation_;
  free_slots_.append(slot);
}
