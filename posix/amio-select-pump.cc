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
#include "posix/amio-select-pump.h"
#include <string.h>
#include <signal.h>
#include <errno.h>

using namespace amio;

SelectMessagePump::SelectMessagePump()
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

ke::Ref<IOError>
SelectMessagePump::Register(ke::Ref<PosixTransport> transport,  ke::Ref<StatusListener> listener)
{
  if (transport->pump())
    return eTransportAlreadyRegistered;
  if (transport->fd() >= FD_SETSIZE)
    return new GenericError("descriptor %d is above FD_SETSIZE (%d)", transport->fd(), FD_SETSIZE);

  assert(size_t(transport->fd()) < max_listeners_);
  transport->setPump(this);
  listeners_[transport->fd()].transport = transport;
  listeners_[transport->fd()].listener = listener;
  return nullptr;
}

bool
SelectMessagePump::Poll(struct timeval *timeoutp)
{
  // Bail out early if there's nothing to listen for.
  if (fd_watermark_ == 0)
    return true;

  // Copy the descriptor maps.
  fd_set read_fds = read_fds_;
  fd_set write_fds = write_fds_;
  int result = select(fd_watermark_, &read_fds, &write_fds, nullptr, timeoutp);
  if (result == -1)
    return new PosixError(errno);

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

  return true;
}

bool
SelectMessagePump::handleRead(int fd)
{
  // We pre-emptively remove this descriptor to simulate edge-triggering.
  FD_CLR(fd, &read_fds_);

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
SelectMessagePump::onWouldBlockRead(int fd)
{
  assert(listeners_[fd].transport);
  FD_SET(fd, &read_fds_);
}

void
SelectMessagePump::onWouldBlockWrite(int fd)
{
  assert(listeners_[fd].transport);
  FD_SET(fd, &write_fds_);
}

void
SelectMessagePump::onClose(int fd)
{
  FD_CLR(fd, &read_fds_);
  FD_CLR(fd, &write_fds_);
  listeners_[fd].transport = nullptr;
  listeners_[fd].listener = nullptr;
}
