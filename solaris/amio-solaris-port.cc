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
   generation_(0),
   max_events_(0)
{
}

PassRef<IOError>
PortImpl::Initialize(size_t maxEventsPerPoll)
{
  if ((port_ = port_create()) == -1)
    return new PosixError();

  if (maxEventsPerPoll == 0)
    maxEventsPerPoll = kDefaultMaxEventsPerPortPoll; 

  max_events_ = maxEventsPerPoll;
  event_buffer_ = new port_event_t[max_events_];
  if (!event_buffer_)
    return eOutOfMemory;

  return nullptr;
}

PortImpl::~PortImpl()
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
PortImpl::Attach(Ref<Transport> baseTransport, Ref<StatusListener> listener, EventFlags eventMask)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport)
    return eIncompatibleTransport;
  if (transport->pump())
    return eTransportAlreadyAttached;
  if (transport->fd() == -1)
    return eTransportClosed;

  assert(listener);

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
  fds_[slot].events = 0;
  fds_[slot].associated = false;
  transport->attach(this, listener);
  transport->setUserData(slot);

  // Set up initial events. If this fails, we unhook the transport and return
  // an error.
  if (eventMask) {
    int events = 0;
    if (eventMask & Event_Read)
      events |= POLLIN;
    if (eventMask & Event_Write)
      events |= POLLOUT;

    Ref<IOError> error = addEventFlags(slot, transport->fd(), events);
    if (error) {
      unhook(transport);
      return error;
    }
  }

  return nullptr;
}

void
PortImpl::Detach(Ref<Transport> baseTransport)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport || transport->pump() != this || transport->fd() == -1)
    return;

  unhook(transport);
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

  // Although port_getn will block for at least |nevents|, apparently it can
  // return more.
  uint_t nevents = 1;
  if (port_getn(port_, event_buffer_, max_events_, &nevents, timeoutp) == -1)
    return new PosixError();

  generation_++;
  for (uint_t i = 0; i < nevents; i++) {
    if (event_buffer_[i].portev_source != PORT_SOURCE_FD)
      continue;

    size_t slot = size_t(event_buffer_[i].portev_user);
    if (fds_[slot].modified == generation_)
      continue;
    assert(int(event_buffer_[i].portev_object) == fds_[slot].transport->fd());

    int events = event_buffer_[i].portev_events;
    if (events & (POLLHUP|POLLERR)) {
      Ref<PosixTransport> transport = fds_[slot].transport;
      Ref<StatusListener> listener = transport->listener();
      unhook(transport);
      if (events & POLLHUP)
        listener->OnHangup(transport);
      else
        listener->OnError(transport, eUnknownHangup);
      continue;
    }

    // We should have gotten at least one event, but it should not be more
    // than one event.
    assert(events & (POLLIN|POLLOUT));
    assert((events & (POLLIN|POLLOUT)) != (POLLIN|POLLOUT));

    // Clear the event we just received. Note that port_get automatically
    // unassociates the descriptor.
    fds_[slot].events &= ~(events & (POLLIN|POLLOUT));
    fds_[slot].associated = false;
    if (events & POLLIN)
      fds_[slot].transport->listener()->OnReadReady(fds_[slot].transport);
    else if (events & POLLOUT)
      fds_[slot].transport->listener()->OnWriteReady(fds_[slot].transport);
  }

  return nullptr;
}

void
PortImpl::Interrupt()
{
  // Not yet implemented.
  abort();
}

PassRef<IOError>
PortImpl::addEventFlags(size_t slot, int fd, int flags)
{
  // Should have filtered already.
  assert((fds_[slot].events & flags) != flags);

  // If the port is already associated, disassociate it.
  if (fds_[slot].associated) {
    port_dissociate(port_, PORT_SOURCE_FD, fd);
    fds_[slot].associated = false;
  }

  // Combine flags we're already waiting for, with the new flags we want to
  // see.
  int all_events = flags | fds_[slot].events;
  int rv = port_associate(
    port_,
    PORT_SOURCE_FD,
    fd,
    all_events,
    (void *)slot);
  if (rv == -1)
    return new PosixError();

  fds_[slot].events = flags;
  fds_[slot].associated = true;
  return nullptr;
}

PassRef<IOError>
PortImpl::onReadWouldBlock(PosixTransport *transport)
{
  size_t slot = transport->getUserData();
  assert(fds_[slot].transport == transport);

  if (!(fds_[slot].events & POLLIN))
    return addEventFlags(slot, transport->fd(), POLLIN);
  return nullptr;
}

PassRef<IOError>
PortImpl::onWriteWouldBlock(PosixTransport *transport)
{
  size_t slot = transport->getUserData();
  assert(fds_[slot].transport == transport);

  if (!(fds_[slot].events & POLLOUT))
    return addEventFlags(slot, transport->fd(), POLLOUT);
  return nullptr;
}

void
PortImpl::unhook(PosixTransport *transport)
{
  assert(transport->pump() == this);

  int fd = transport->fd();
  size_t slot = transport->getUserData();
  assert(fd != -1);
  assert(fds_[slot].transport == transport);

  if (fds_[slot].associated)
    port_dissociate(port_, PORT_SOURCE_FD, fd);

  // Just for safety, we detach here in case the assignment below drops the
  // last ref to the transport.
  transport->detach();

  fds_[slot].transport = nullptr;
  fds_[slot].modified = generation_;
  free_slots_.append(slot);
}
