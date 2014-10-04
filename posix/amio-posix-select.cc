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
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport)
    return eIncompatibleTransport;
  if (transport->pump())
    return eTransportAlreadyAttached;
  if (transport->fd() >= FD_SETSIZE)
    return new GenericError("descriptor %d is above FD_SETSIZE (%d)", transport->fd(), FD_SETSIZE);
  if (transport->fd() == -1)
    return eTransportClosed;

  assert(listener);
  assert(size_t(transport->fd()) < max_fds_);

  transport->attach(this, listener);
  fds_[transport->fd()].transport = transport;
  fds_[transport->fd()].modified = generation_;

  if (transport->fd() > fd_watermark_)
    fd_watermark_ = transport->fd();

  if (eventMask & Event_Read)
    FD_SET(transport->fd(), &read_fds_);
  if (eventMask & Event_Write)
    FD_SET(transport->fd(), &write_fds_);
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
  for (size_t i = 0; i <= size_t(fd_watermark_); i++) {
    // Make sure this transport wasn't swapped out or removed.
    if (!isEventValid(i))
      continue;

    if (FD_ISSET(i, &read_fds)) {
      // Pre-emptively remove this descriptor to simulate edge-triggering.
      FD_CLR(i, &read_fds_);

      fds_[i].transport->listener()->OnReadReady(fds_[i].transport);
      if (!isEventValid(i))
        continue;
    }
    if (FD_ISSET(i, &write_fds)) {
      // Pre-emptively remove this descriptor to simulate edge-triggering.
      FD_CLR(i, &write_fds_);

      fds_[i].transport->listener()->OnWriteReady(fds_[i].transport);
    }
  }

  return nullptr;
}

PassRef<IOError>
SelectImpl::onReadWouldBlock(PosixTransport *transport)
{
  int fd = transport->fd();
  assert(fds_[fd].transport);
  FD_SET(fd, &read_fds_);
  return nullptr;
}

PassRef<IOError>
SelectImpl::onWriteWouldBlock(PosixTransport *transport)
{
  int fd = transport->fd();
  assert(fds_[fd].transport);
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
