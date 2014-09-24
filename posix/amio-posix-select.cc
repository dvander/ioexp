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

SelectMessagePump::SelectMessagePump()
 : fd_watermark_(-1),
   max_listeners_(FD_SETSIZE)
{
  FD_ZERO(&read_fds_);
  FD_ZERO(&write_fds_);
  max_listeners_ = FD_SETSIZE;
  listeners_ = new SelectData[max_listeners_];
  memset(listeners_, 0, sizeof(SelectData) * max_listeners_);
}

SelectMessagePump::~SelectMessagePump()
{
  for (size_t i = 0; i < max_listeners_; i++) {
    if (listeners_[i].transport)
      listeners_[i].transport->setPump(nullptr);
  }
}

Ref<IOError>
SelectMessagePump::Register(Ref<Transport> baseTransport, Ref<StatusListener> listener)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport)
    return eIncompatibleTransport;
  if (transport->pump())
    return eTransportAlreadyRegistered;
  if (transport->fd() >= FD_SETSIZE)
    return new GenericError("descriptor %d is above FD_SETSIZE (%d)", transport->fd(), FD_SETSIZE);
  if (transport->fd() == -1)
    return eTransportClosed;

  assert(listener);
  assert(size_t(transport->fd()) < max_listeners_);

  transport->setPump(this);
  listeners_[transport->fd()].transport = transport;
  listeners_[transport->fd()].listener = listener;

  if (transport->fd() > fd_watermark_)
    fd_watermark_ = transport->fd();

  // Automatically listen for reads, so it's possible to simply wait for data.
  // We don't bother listening for writes since we can always just try calling
  // Transport::Write().
  FD_SET(transport->fd(), &read_fds_);
  return nullptr;
}

Ref<IOError>
SelectMessagePump::Initialize()
{
  return nullptr;
}

void
SelectMessagePump::Deregister(Ref<Transport> baseTransport)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (transport->fd() == -1)
    return;

  onClose(transport->fd());
}

Ref<IOError>
SelectMessagePump::Poll(int timeoutMs)
{
  // Bail out early if there's nothing to listen for.
  if (fd_watermark_ == -1)
    return nullptr;

  struct timeval timeout;
  struct timeval *timeoutp = nullptr;
  if (timeoutMs >= 0) {
    timeout.tv_sec = 0;
    timeout.tv_usec = timeoutMs * 1000;
    timeoutp = &timeout;
  }

  // Copy the descriptor maps.
  fd_set read_fds = read_fds_;
  fd_set write_fds = write_fds_;
  int result = select(fd_watermark_ + 1, &read_fds, &write_fds, nullptr, timeoutp);
  if (result == -1)
    return new PosixError();

  for (size_t i = 0; i < max_listeners_; i++) {
    if (!listeners_[i].transport)
      continue;

    if (FD_ISSET(i, &read_fds)) {
      if (!handleRead(i))
        continue;
    }
    if (FD_ISSET(i, &write_fds)) {
      if (!handleWrite(i))
        continue;
    }
  }

  return nullptr;
}

bool
SelectMessagePump::handleRead(int fd)
{
  listeners_[fd].listener->OnReadReady(listeners_[fd].transport);
  return !!listeners_[fd].transport;
}

bool
SelectMessagePump::handleWrite(int fd)
{
  // We pre-emptively remove this descriptor to simulate edge-triggering.
  FD_CLR(fd, &write_fds_);

  listeners_[fd].listener->OnWriteReady(listeners_[fd].transport);
  return !!listeners_[fd].transport;
}

void
SelectMessagePump::onReadWouldBlock(int fd)
{
  assert(listeners_[fd].transport);
  FD_SET(fd, &read_fds_);
}

void
SelectMessagePump::onWriteWouldBlock(int fd)
{
  assert(listeners_[fd].transport);
  FD_SET(fd, &write_fds_);
}

void
SelectMessagePump::onClose(int fd)
{
  if (!listeners_[fd].transport || listeners_[fd].transport->pump() != this)
    return;

  FD_CLR(fd, &read_fds_);
  FD_CLR(fd, &write_fds_);
  listeners_[fd].transport = nullptr;
  listeners_[fd].listener = nullptr;

  // If this was the watermark, find a new one.
  if (fd == fd_watermark_) {
    fd_watermark_ = -1;
    for (int i = fd - 1; i >= 0; i--) {
      if (listeners_[i].transport) {
        fd_watermark_ = i;
        break;
      }
    }
  }
}

void
SelectMessagePump::Interrupt()
{
  // Not yet implemented.
  abort();
}
