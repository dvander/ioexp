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
#include "bsd/amio-bsd-kqueue.h"
#include <unistd.h>

using namespace ke;
using namespace amio;

KqueueMessagePump::KqueueMessagePump(size_t maxEvents)
 : kq_(-1),
   generation_(0),
   max_events_(maxEvents)
{
}

KqueueMessagePump::~KqueueMessagePump()
{
  if (kq_ == -1)
    return;

  for (size_t i = 0; i < listeners_.length(); i++) {
    if (listeners_[i].transport)
      listeners_[i].transport->setPump(nullptr);
  }

  close(kq_);
}

Ref<IOError>
KqueueMessagePump::Initialize()
{
  if ((kq_ = kqueue()) == -1)
    return new PosixError();

  event_buffer_ = new struct kevent[max_events_];
  if (!event_buffer_)
    return eOutOfMemory;

  return nullptr;
}

Ref<IOError>
KqueueMessagePump::Register(Ref<Transport> baseTransport, Ref<StatusListener> listener)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport)
    return eIncompatibleTransport;
  if (transport->pump())
    return eTransportAlreadyRegistered;
  if (transport->fd() == -1)
    return eTransportClosed;

  assert(listener);

  size_t slot;
  if (!free_slots_.empty()) {
    slot = free_slots_.popCopy();
  } else {
    slot = listeners_.length();
    if (!listeners_.append(PollData()))
      return eOutOfMemory;
  }

  struct kevent event;
  EV_SET(&event, transport->fd(), EVFILT_READ, EV_ADD|EV_CLEAR, 0, 0, (void *)slot);
  if (kevent(kq_, &event, 1, nullptr, 0, nullptr) == -1) {
    Ref<IOError> error = new PosixError();
    free_slots_.append(slot);
    return error;
  }

  // Hook up the transport.
  listeners_[slot].transport = transport;
  listeners_[slot].listener = listener;
  listeners_[slot].modified = generation_;
  listeners_[slot].watching_writes = false;
  transport->setPump(this);
  transport->setUserData(slot);
  return nullptr;
}

void
KqueueMessagePump::Deregister(Ref<Transport> baseTransport)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport || transport->pump() != this || transport->fd() == -1)
    return;

  unhook(transport);
}

Ref<IOError>
KqueueMessagePump::Poll(int timeoutMs)
{
  struct timespec timeout;
  struct timespec *timeoutp = nullptr;
  if (timeoutMs >= 0) {
    timeout.tv_sec = 0;
    timeout.tv_nsec = timeoutMs * 1000000;
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
      PollData data = listeners_[slot];
      unhook(data.transport);
      data.listener->OnHangup(data.transport);
      continue;
    }

    switch (ev.filter) {
     case EVFILT_READ:
      listeners_[slot].listener->OnReadReady(listeners_[slot].transport);
      break;

     case EVFILT_WRITE:
      printf("got here\n");
      listeners_[slot].listener->OnWriteReady(listeners_[slot].transport);
      break;

     default:
      assert(false);
    }
  }

  return nullptr;
}

void
KqueueMessagePump::Interrupt()
{
  // Not yet implemented.
  abort();
}

void
KqueueMessagePump::onReadWouldBlock(PosixTransport *transport)
{
  // Do nothing... kqueue is edge-triggered.
}

PassRef<IOError>
KqueueMessagePump::onWriteWouldBlock(PosixTransport *transport)
{
  // By default we don't listen to writes until a write is requested.
  size_t slot = transport->getUserData();
  if (!listeners_[slot].watching_writes) {
    struct kevent event;
    EV_SET(&event, transport->fd(), EVFILT_WRITE, EV_ADD|EV_CLEAR, 0, 0, (void *)slot);
    if (kevent(kq_, &event, 1, nullptr, 0, nullptr) == -1)
      return new PosixError();
    listeners_[slot].watching_writes = true;
  }

  return nullptr;
}

void
KqueueMessagePump::unhook(Ref<PosixTransport> transport)
{
  assert(transport->pump() == this);

  int fd = transport->fd();
  uintptr_t slot = transport->getUserData();
  assert(fd != -1);
  assert(listeners_[slot].transport == transport);

  size_t nevents = 1;
  struct kevent events[2];
  EV_SET(&events[0], transport->fd(), EVFILT_READ, EV_DELETE, 0, 0, (void *)slot);
  if (listeners_[slot].watching_writes) {
    EV_SET(&events[1], transport->fd(), EVFILT_WRITE, EV_DELETE, 0, 0, (void *)slot);
    nevents++;
  }
  kevent(kq_, events, nevents, nullptr, 0, nullptr);

  listeners_[slot].transport = nullptr;
  listeners_[slot].listener = nullptr;
  listeners_[slot].modified = generation_;
  transport->setPump(nullptr);
  free_slots_.append(slot);
}
