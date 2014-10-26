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
#include "solaris/amio-solaris-port.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/devpoll.h>
#include <stropts.h>

using namespace ke;
using namespace amio;

PortImpl::PortImpl()
 : port_(-1),
   generation_(0)
{
}

PassRef<IOError>
PortImpl::Initialize(size_t maxEventsPerPoll)
{
  if ((port_ = port_create()) == -1)
    return new PosixError();

  if (!event_buffers_.init(32, maxEventsPerPoll))
    return eOutOfMemory;

  return nullptr;
}

PortImpl::~PortImpl()
{
  Shutdown();
}

void
PortImpl::Shutdown()
{
  if (port_ == -1)
    return;

  for (size_t i = 0; i < fds_.length(); i++) {
    if (fds_[i].transport)
      fds_[i].transport->detach();
  }

  close(port_);
}

PassRef<IOError>
PortImpl::attach_locked(PosixTransport *transport, StatusListener *listener, TransportFlags flags)
{
  size_t slot;
  if (free_slots_.empty()) {
    slot = fds_.length();
    if (!fds_.append(PollData()))
      return eOutOfMemory;
  } else {
    slot = free_slots_.popCopy();
  }

  // Hook up the transport.
  fds_[slot].transport = transport;
  fds_[slot].modified = generation_;
  transport->attach(this, listener);
  transport->setUserData(slot);

  if (Ref<IOError> error = change_events_locked(transport, flags)) {
    detach_locked(transport);
    return error;
  }

  return nullptr;
}

void
PortImpl::detach_locked(PosixTransport *transport)
{
  int fd = transport->fd();
  size_t slot = transport->getUserData();
  assert(fd != -1);
  assert(fds_[slot].transport == transport);

  if (transport->flags() & kTransportArmed)
    port_dissociate(port_, PORT_SOURCE_FD, fd);

  // Just for safety, we detach here in case the assignment below drops the
  // last ref to the transport.
  transport->detach();

  fds_[slot].transport = nullptr;
  fds_[slot].modified = generation_;
  free_slots_.append(slot);
}

PassRef<IOError>
PortImpl::change_events_locked(PosixTransport *transport, TransportFlags flags)
{
  int events = 0;
  if (flags & kTransportReading)
    events |= POLLIN;
  if (flags & kTransportWriting)
    events |= POLLOUT;

  int rv = port_associate(
    port_,
    PORT_SOURCE_FD,
    transport->fd(),
    events,
    (void *)transport->getUserData());
  if (rv == -1)
    return new PosixError();

  transport->flags() |= flags;
  transport->flags() |= kTransportArmed;
  return nullptr;
}

template <TransportFlags outFlag>
inline void
PortImpl::handleEvent(size_t slot)
{
  Ref<PosixTransport> transport = fds_[slot].transport;
  if (!(transport->flags() & outFlag))
    return;

  // If edge-triggered, we don't want to re-arm later, so take the flag off.
  if (transport->flags() & kTransportET)
    transport->flags() &= ~outFlag;

  // Port is no longer armed after port_get().
  transport->flags() &= ~kTransportArmed;

  {
    // We must hold the listener in a ref, since if the transport is detached
    // in the callback, it could be destroyed while |this| is still on the
    // stack. Similarly, we must hold the transport in a ref in case releasing
    // the lock allows a detach to happen.
    Ref<StatusListener> listener = transport->listener();

    AutoMaybeUnlock unlock(lock_);

    if (outFlag == kTransportReading)
      listener->OnReadReady(transport);
    else if (outFlag == kTransportWriting)
      listener->OnWriteReady(transport);
  }

  // Don't re-arm if the fd changed or the port is already re-armed.
  if (isFdChanged(slot) || (transport->flags() & kTransportArmed))
    return;

  // Note: we rely on change_events_locked() not checking prior flags.
  if (Ref<IOError> error = change_events_locked(transport, transport->flags() & kTransportEventMask))
    reportError_locked(transport, error);
}

PassRef<IOError>
PortImpl::Poll(int timeoutMs)
{
  timespec_t timeout;
  timespec_t *timeoutp = nullptr;
  if (timeoutMs != kNoTimeout) {
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_nsec = (timeoutMs % 1000) * 1000000;
    timeoutp = &timeout;
  }

  // Note: no poll lock, we're concurrent.
  MultiPollBuffer<port_event_t>::Use use(event_buffers_);

  PollBuffer<port_event_t> *event_buffer = use.get();
  if (!event_buffer)
    return eOutOfMemory;

  // Although port_getn will block for at least |nevents|, apparently it can
  // return more.
  uint_t nevents = 1;
  if (port_getn(port_, event_buffer->get(), event_buffer->length(), &nevents, timeoutp) == -1) {
    if (errno == ETIME)
      return nullptr;
    return new PosixError();
  }

  AutoMaybeLock lock(lock_);

  generation_++;
  for (uint_t i = 0; i < nevents; i++) {
    const port_event_t &event = event_buffer->at(i);
    if (event.portev_source != PORT_SOURCE_FD)
      continue;

    size_t slot = size_t(event.portev_user);
    if (isFdChanged(slot))
      continue;
    assert(int(event.portev_object) == fds_[slot].transport->fd());

    int events = event.portev_events;
    if (events & POLLHUP) {
      reportHup_locked(fds_[slot].transport);
      continue;
    }
    if (events & POLLERR) {
      reportError_locked(fds_[slot].transport);
      continue;
    }

    // We should have gotten at least one event, but it should not be more
    // than one event.
    assert(events & (POLLIN|POLLOUT));
    assert((events & (POLLIN|POLLOUT)) != (POLLIN|POLLOUT));

    // Clear the event we just received. Note that port_get automatically
    // unassociates the descriptor.
    if (events & POLLIN)
      handleEvent<kTransportReading>(slot);
    else if (events & POLLOUT)
      handleEvent<kTransportWriting>(slot);
  }

  if (nevents == event_buffer->length())
    event_buffer->maybeResize();

  return nullptr;
}
