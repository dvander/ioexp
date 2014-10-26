// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "shared/shared-errors.h"
#include "posix/posix-errors.h"
#include "posix/posix-select.h"
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
  Shutdown();
}

void
SelectImpl::Shutdown()
{
  AutoMaybeLock lock(lock_);

  for (size_t i = 0; i < max_fds_; i++) {
    if (fds_[i].transport)
      detach_for_shutdown_locked(fds_[i].transport);
  }
}

PassRef<IOError>
SelectImpl::attach_locked(PosixTransport *transport, StatusListener *listener, TransportFlags flags)
{
  if (transport->fd() >= FD_SETSIZE)
    return new GenericError("descriptor %d is above FD_SETSIZE (%d)", transport->fd(), FD_SETSIZE);
  assert(size_t(transport->fd()) < max_fds_);

  select_ctl(transport->fd(), flags);

  transport->flags() |= flags;
  transport->attach(this, listener);
  fds_[transport->fd()].transport = transport;
  fds_[transport->fd()].modified = generation_;

  if (transport->fd() > fd_watermark_)
    fd_watermark_ = transport->fd();
  return nullptr;
}

PassRef<StatusListener>
SelectImpl::detach_locked(PosixTransport *transport)
{
  int fd = transport->fd();
  assert(fd != -1);
  assert(fds_[fd].transport == transport);

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

  return transport->detach();
}

PassRef<IOError>
SelectImpl::change_events_locked(PosixTransport *transport, TransportFlags flags)
{
  int fd = transport->fd();

  select_ctl(fd, flags);
  transport->flags() &= ~kTransportEventMask;
  transport->flags() |= flags;
  return nullptr;
}

void
SelectImpl::select_ctl(int fd, TransportFlags flags)
{
  if (flags & kTransportReading)
    FD_SET(fd, &read_fds_);
  else
    FD_CLR(fd, &read_fds_);
  if (flags & kTransportWriting)
    FD_SET(fd, &write_fds_);
  else
    FD_CLR(fd, &write_fds_);
}

template <TransportFlags outFlag>
inline void
SelectImpl::handleEvent(fd_set *set, int fd)
{
  Ref<PosixTransport> transport = fds_[fd].transport;
  if (transport->flags() & kTransportLT) {
    // Ignore - the event's been changed.
    if (!(transport->flags() & outFlag))
      return;
  } else {
    // Remove the descriptor to simulate edge-triggering.
    FD_CLR(fd, set);
    transport->flags() &= ~outFlag;
  }

  // We must hold the listener in a ref, since if the transport is detached
  // in the callback, it could be destroyed while |this| is still on the
  // stack. Similarly, we must hold the transport in a ref in case releasing
  // the lock allows a detach to happen.
  Ref<StatusListener> listener = transport->listener();

  AutoMaybeUnlock unlock(lock_);
  if (outFlag == kTransportReading)
    listener->OnReadReady();
  else if (outFlag == kTransportWriting)
    listener->OnWriteReady();
}

PassRef<IOError>
SelectImpl::Poll(int timeoutMs)
{
  // We acquire a lock specifically for Poll(), to make sure it isn't called on
  // any thread or within callbacks.
  AutoMaybeLock poll_lock(poll_lock_);

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

  fd_set read_fds, write_fds;
  int fd_watermark;

  // Copy the descriptor maps. Do this in a lock, so we don't have to hold the
  // transport lock while polling.
  {
    AutoMaybeLock lock(lock_);
    read_fds = read_fds_;
    write_fds = write_fds_;
    fd_watermark = fd_watermark_;
  }

  int result = select(fd_watermark + 1, &read_fds, &write_fds, nullptr, timeoutp);
  if (result == -1)
    return new PosixError();

  AutoMaybeLock lock(lock_);

  generation_++;
  for (int i = 0; i <= fd_watermark; i++) {
    // Make sure this transport wasn't swapped out or removed.
    if (isFdChanged(i))
      continue;

    if (FD_ISSET(i, &read_fds)) {
      handleEvent<kTransportReading>(&read_fds_, i);
      if (isFdChanged(i))
        continue;
    }
    if (FD_ISSET(i, &write_fds))
      handleEvent<kTransportWriting>(&write_fds_, i);
  }

  return nullptr;
}
