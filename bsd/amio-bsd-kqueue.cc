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

  // Pre-fill the two kevent structs beforehand.
  EV_SET(&listeners_[slot].read, transport->fd(), EVFILT_READ, EV_ADD|EV_CLEAR|EV_DISABLE, 0, 0, kev_userdata_t(slot));
  EV_SET(&listeners_[slot].write, transport->fd(), EVFILT_WRITE, EV_ADD|EV_CLEAR|EV_DISABLE, 0, 0, kev_userdata_t(slot));

  int nevents = 0;
  struct kevent events[2];
  if (eventMask & Event_Read) {
    listeners_[slot].read.flags &= ~EV_DISABLE;
    events[nevents++] = listeners_[slot].read;
  }
  if (eventMask & Event_Write) {
    listeners_[slot].write.flags &= ~EV_DISABLE;
    events[nevents++] = listeners_[slot].write;
  }

  if (nevents && kevent(kq_, events, nevents, nullptr, 0, nullptr) == -1) {
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
    if (!isEventValid(slot))
      continue;

    if (ev.flags & EV_EOF) {
      // Get a local copy of the poll data before we wipe it out.
      Ref<PosixTransport> transport = listeners_[slot].transport;
      Ref<StatusListener> listener = transport->listener();
      unhook(transport);
      listener->OnHangup(transport);
      continue;
    }

    PollData &data = listeners_[slot];
    switch (ev.filter) {
     case EVFILT_READ:
      data.transport->listener()->OnReadReady(data.transport);
      break;

     case EVFILT_WRITE:
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
  if (listeners_[slot].read.flags & EV_DISABLE) {
    listeners_[slot].read.flags &= ~EV_DISABLE;
    if (kevent(kq_, &listeners_[slot].read, 1, nullptr, 0, nullptr) == -1) {
      listeners_[slot].read.flags |= EV_DISABLE;
      return new PosixError();
    }
  }
  return nullptr;
}

PassRef<IOError>
KqueueImpl::onWriteWouldBlock(PosixTransport *transport)
{
  size_t slot = transport->getUserData();
  if (listeners_[slot].write.flags & EV_DISABLE) {
    listeners_[slot].write.flags &= ~EV_DISABLE;
    if (kevent(kq_, &listeners_[slot].write, 1, nullptr, 0, nullptr) == -1) {
      listeners_[slot].write.flags |= EV_DISABLE;
      return new PosixError();
    }
  }
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

  int nevents = 0;
  struct kevent events[2];
  if (!(listeners_[slot].read.flags & EV_DISABLE)) {
    listeners_[slot].read.flags = EV_DELETE;
    events[nevents++] = listeners_[slot].read;
  }
  if (!(listeners_[slot].write.flags & EV_DISABLE)) {
    listeners_[slot].write.flags = EV_DELETE;
    events[nevents++] = listeners_[slot].write;
  }
  kevent(kq_, events, nevents, nullptr, 0, nullptr);

  // Detach here, just for safety - in case null below frees it.
  transport->detach();

  listeners_[slot].transport = nullptr;
  listeners_[slot].modified = generation_;
  free_slots_.append(slot);
}
