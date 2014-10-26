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
#include <limits.h>

using namespace ke;
using namespace amio;

#if defined(__NetBSD__)
typedef intptr_t kev_userdata_t;
#else
typedef void* kev_userdata_t;
#endif

KqueueImpl::KqueueImpl()
 : kq_(-1),
   generation_(0)
{
}

KqueueImpl::~KqueueImpl()
{
  Shutdown();
}

void
KqueueImpl::Shutdown()
{
  AutoMaybeLock lock(lock_);

  if (kq_ == -1)
    return;

  for (size_t i = 0; i < listeners_.length(); i++) {
    if (listeners_[i].transport)
      detach_for_shutdown(listeners_[i].transport);
  }

  close(kq_);
}

PassRef<IOError>
KqueueImpl::Initialize(size_t absoluteMaxEvents)
{
  if ((kq_ = kqueue()) == -1)
    return new PosixError();

  if (!event_buffer_.init(32, absoluteMaxEvents))
    return eOutOfMemory;

  return nullptr;
}

PassRef<IOError>
KqueueImpl::attach_locked(PosixTransport *transport, StatusListener *listener, TransportFlags flags)
{
  size_t slot;
  if (!free_slots_.empty()) {
    slot = free_slots_.popCopy();
  } else {
    slot = listeners_.length();
    if (!listeners_.append(PollData()))
      return eOutOfMemory;
  }

  // Hook up the transport.
  listeners_[slot].transport = transport;
  listeners_[slot].modified = generation_;
  transport->attach(this, listener);
  transport->setUserData(slot);

  if (Ref<IOError> error = change_events_locked(transport, flags)) {
    // Note: don't call OnChangeProxy, we never fully attached.
    detach_locked(transport);
    return error;
  }

  return nullptr;
}

PassRef<IOError>
KqueueImpl::change_events_locked(PosixTransport *transport, TransportFlags flags)
{
  int fd = transport->fd();
  size_t slot = transport->getUserData();
  kev_userdata_t data = kev_userdata_t(slot);

  int nchanges = 0;
  struct kevent changes[2];

  int extra = (flags & kTransportET) ? EV_CLEAR : 0;

  if ((flags & kTransportReading) != (transport->flags() & kTransportReading)) {
    if (flags & kTransportReading)
      EV_SET(&changes[nchanges], fd, EVFILT_READ, EV_ADD|EV_ENABLE|extra, 0, 0, data);
    else
      EV_SET(&changes[nchanges], fd, EVFILT_READ, EV_DELETE, 0, 0, data);
    nchanges++;
  }
  if ((flags & kTransportWriting) != (transport->flags() & kTransportWriting)) {
    if (flags & kTransportWriting)
      EV_SET(&changes[nchanges], fd, EVFILT_WRITE, EV_ADD|EV_ENABLE|extra, 0, 0, data);
    else
      EV_SET(&changes[nchanges], fd, EVFILT_WRITE, EV_DELETE, 0, 0, data);
    nchanges++;
  }

  if (kevent(kq_, changes, nchanges, nullptr, 0, nullptr) == -1)
    return new PosixError();

  transport->flags() &= ~kTransportEventMask;
  transport->flags() |= flags;
  return nullptr;
}

PassRef<StatusListener>
KqueueImpl::detach_locked(PosixTransport *transport)
{
  int fd = transport->fd();
  uintptr_t slot = transport->getUserData();
  assert(fd != -1);
  assert(listeners_[slot].transport == transport);

  change_events_locked(transport, kTransportNoFlags);

  listeners_[slot].transport = nullptr;
  listeners_[slot].modified = generation_;
  free_slots_.append(slot);

  return transport->detach();
}

PassRef<IOError>
KqueueImpl::Poll(int timeoutMs)
{
  AutoMaybeLock poll_lock(poll_lock_);

  struct timespec timeout;
  struct timespec *timeoutp = nullptr;
  if (timeoutMs >= 0) {
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_nsec = (timeoutMs % 1000) * 1000000;
    timeoutp = &timeout;
  }

  int nevents = kevent(kq_, nullptr, 0, event_buffer_.get(), event_buffer_.length(), timeoutp);
  if (nevents == -1)
    return new PosixError();

  AutoMaybeLock lock(lock_);

  generation_++;
  for (int i = 0; i < nevents; i++) {
    struct kevent &ev = event_buffer_[i];
    size_t slot = (size_t)ev.udata;
    if (isFdChanged(slot))
      continue;

    PollData &data = listeners_[slot];
    if (ev.flags & EV_EOF) {
      reportHup_locked(data.transport);
      continue;
    }
    if (ev.flags & EV_ERROR) {
      reportError_locked(data.transport, new PosixError(ev.data));
      continue;
    }

    switch (ev.filter) {
      case EVFILT_READ:
      {
        Ref<PosixTransport> transport = data.transport;
        if (!(transport->flags() & kTransportReading))
          continue;

        Ref<StatusListener> listener = transport->listener();

        AutoMaybeUnlock unlock(lock_);
        listener->OnReadReady(transport);
        break;
      }

      case EVFILT_WRITE:
      {
        Ref<PosixTransport> transport = data.transport;
        if (!(transport->flags() & kTransportWriting))
          continue;

        Ref<StatusListener> listener = transport->listener();

        AutoMaybeUnlock unlock(lock_);
        listener->OnWriteReady(transport);
        break;
      }

      default:
        assert(false);
    }
  }

  // If we filled the event buffer, resize it for next time.
  if (size_t(nevents) == event_buffer_.length())
    event_buffer_.maybeResize();

  return nullptr;
}
