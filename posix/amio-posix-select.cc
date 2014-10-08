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
#include "posix/amio-posix-select.h"
#include <string.h>
#include <signal.h>

using namespace amio;
using namespace ke;

SelectImpl::SelectImpl()
 : fd_watermark_(-1),
   max_fds_(FD_SETSIZE),
   generation_(0)
{
  FD_ZERO(&read_fds_);
  FD_ZERO(&write_fds_);
  max_fds_ = FD_SETSIZE;
  fds_ = new SelectData[max_fds_];
  memset(fds_, 0, sizeof(SelectData) * max_fds_);
}

SelectImpl::~SelectImpl()
{
  for (size_t i = 0; i < max_fds_; i++) {
    if (fds_[i].transport)
      fds_[i].transport->detach();
  }
}

PassRef<IOError>
SelectImpl::Attach(Ref<Transport> baseTransport, Ref<StatusListener> listener, EventFlags eventMask)
{
  PosixTransport *transport;
  Ref<IOError> error = toPosixTransport(&transport, baseTransport);
  if (error)
    return error;

  if (transport->fd() >= FD_SETSIZE)
    return new GenericError("descriptor %d is above FD_SETSIZE (%d)", transport->fd(), FD_SETSIZE);

  assert(listener);
  assert(size_t(transport->fd()) < max_fds_);

  transport->attach(this, listener);
  fds_[transport->fd()].transport = transport;
  fds_[transport->fd()].modified = generation_;
  fds_[transport->fd()].flags = eventMask;

  if (transport->fd() > fd_watermark_)
    fd_watermark_ = transport->fd();

  if (eventMask & Event_Read)
    FD_SET(transport->fd(), &read_fds_);
  if (eventMask & Event_Write)
    FD_SET(transport->fd(), &write_fds_);
  return nullptr;
}

PassRef<IOError>
SelectImpl::ChangeStickyEvents(Ref<Transport> baseTransport, EventFlags eventMask)
{
  Ref<PosixTransport> transport = validateEventChange(baseTransport, eventMask);
  if (!transport)
    return eIncompatibleTransport;

  int fd = transport->fd();
  if (!(fds_[fd].flags & Event_Sticky))
    return eIncompatibleTransport;

  if (eventMask & Event_Read)
    FD_SET(fd, &read_fds_);
  else
    FD_CLR(fd, &read_fds_);
  if (eventMask & Event_Write)
    FD_SET(fd, &write_fds_);
  else
    FD_CLR(fd, &write_fds_);

  fds_[fd].flags = eventMask;
  return nullptr;
}

void
SelectImpl::Detach(Ref<Transport> baseTransport)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport || transport->pump() != this || transport->fd() == -1)
    return;

  unhook(transport);
}

template <EventFlags outFlag>
inline void
SelectImpl::handleEvent(fd_set *set, int fd)
{
  if (fds_[fd].flags & Event_Sticky) {
    if (!(fds_[fd].flags & outFlag))
      return;
  } else {
    // Remove the descriptor to simulate edge-triggering.
    FD_CLR(fd, set);
  }

  if (outFlag == Event_Read)
    fds_[fd].transport->listener()->OnReadReady(fds_[fd].transport);
  else if (outFlag == Event_Write)
    fds_[fd].transport->listener()->OnWriteReady(fds_[fd].transport);
}

PassRef<IOError>
SelectImpl::Poll(int timeoutMs)
{
  // Bail out early if there's nothing to listen for.
  if (fd_watermark_ == -1)
    return nullptr;

  struct timeval timeout;
  struct timeval *timeoutp = nullptr;
  if (timeoutMs >= 0) {
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    timeoutp = &timeout;
  }

  // Copy the descriptor maps.
  fd_set read_fds = read_fds_;
  fd_set write_fds = write_fds_;
  int result = select(fd_watermark_ + 1, &read_fds, &write_fds, nullptr, timeoutp);
  if (result == -1)
    return new PosixError();

  generation_++;
  for (int i = 0; i <= fd_watermark_; i++) {
    // Make sure this transport wasn't swapped out or removed.
    if (isFdChanged(i))
      continue;

    if (FD_ISSET(i, &read_fds)) {
      handleEvent<Event_Read>(&read_fds_, i);
      if (isFdChanged(i))
        continue;
    }
    if (FD_ISSET(i, &write_fds))
      handleEvent<Event_Write>(&write_fds_, i);
  }

  return nullptr;
}

PassRef<IOError>
SelectImpl::onReadWouldBlock(PosixTransport *transport)
{
  int fd = transport->fd();
  assert(fds_[fd].transport == transport);
  FD_SET(fd, &read_fds_);
  return nullptr;
}

PassRef<IOError>
SelectImpl::onWriteWouldBlock(PosixTransport *transport)
{
  int fd = transport->fd();
  assert(fds_[fd].transport == transport);
  FD_SET(fd, &write_fds_);
  return nullptr;
}

void
SelectImpl::unhook(PosixTransport *transport)
{
  int fd = transport->fd();
  assert(fd != -1);
  assert(fds_[fd].transport == transport);
  assert(transport->pump() == this);

  transport->detach();
  FD_CLR(fd, &read_fds_);
  FD_CLR(fd, &write_fds_);
  fds_[fd].transport = nullptr;
  fds_[fd].modified = generation_;

  // If this was the watermark, find a new one.
  if (fd == fd_watermark_) {
    fd_watermark_ = -1;
    for (int i = fd - 1; i >= 0; i--) {
      if (fds_[i].transport) {
        fd_watermark_ = i;
        break;
      }
    }
  }
}

void
SelectImpl::Interrupt()
{
  // Not yet implemented.
  abort();
}
