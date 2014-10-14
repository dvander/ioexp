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
#include "posix/amio-posix-poll.h"
#include "linux/amio-linux.h"
#include <sys/poll.h>
#include <string.h>

#if defined(__linux__) && !defined(POLLRDHUP)
# define POLLRDHUP 0x2000
#endif

using namespace ke;
using namespace amio;

static const size_t kInitialPollSize = 4096;

PollImpl::PollImpl()
 : generation_(0),
   tmp_buffer_len_(0)
{
#if defined(__linux__)
  can_use_rdhup_ = false;
  if (IsAtLeastLinux(2, 6, 17))
    can_use_rdhup_ = true;
#endif
}

PollImpl::~PollImpl()
{
  Shutdown();
}

void
PollImpl::Shutdown()
{
  AutoMaybeLock lock(lock_);

  for (size_t i = 0; i < poll_events_.length(); i++) {
    int fd = poll_events_[i].fd;
    if (fd == -1)
      continue;

    if (fds_[fd].transport) {
      fds_[fd].transport->detach();
      fds_[fd].transport = nullptr;
    }
  }
}

PassRef<IOError>
PollImpl::Initialize()
{
  if (!fds_.resize(kInitialPollSize))
    return eOutOfMemory;
  return nullptr;
}

PassRef<IOError>
PollImpl::attach_locked(PosixTransport *transport, StatusListener *listener, TransportFlags flags)
{
  if (size_t(transport->fd()) >= fds_.length()) {
    if (!fds_.resize(transport->fd() + 1))
      return eOutOfMemory;
  }
  assert(size_t(transport->fd()) < fds_.length());

  int defaultEvents = POLLERR | POLLHUP;
#if defined(__linux__)
  if (can_use_rdhup_)
    defaultEvents |= POLLRDHUP;
#endif

  size_t slot;
  struct pollfd pe;
  pe.fd = transport->fd();
  pe.events = defaultEvents;
  if (free_slots_.empty()) {
    slot = poll_events_.length();
    if (!poll_events_.append(pe))
      return eOutOfMemory;
  } else {
    slot = free_slots_.popCopy();
    poll_events_[slot] = pe;
  }
  poll_ctl(slot, flags);

  transport->attach(this, listener);
  transport->flags() |= flags;
  transport->setUserData(slot);
  fds_[transport->fd()].transport = transport;
  fds_[transport->fd()].modified = generation_;
  return nullptr;
}

void
PollImpl::detach_locked(PosixTransport *transport)
{
  int fd = transport->fd();
  assert(fd != -1);
  assert(fds_[fd].transport == transport);

  size_t slot = transport->getUserData();
  assert(poll_events_[slot].fd == fd);

  // Note: just for safety, we call this after we're done with the transport,
  // in case the assignment below drops the last ref.
  transport->detach();

  poll_events_[slot].fd = -1;
  fds_[fd].transport = nullptr;
  fds_[fd].modified = generation_;
  free_slots_.append(slot);
}

PassRef<IOError>
PollImpl::change_events_locked(PosixTransport *transport, TransportFlags flags)
{
  size_t slot = transport->getUserData();
  poll_ctl(slot, flags);
  transport->flags() &= ~kTransportEventMask;
  transport->flags() |= flags;
  return nullptr;
}

void
PollImpl::poll_ctl(size_t slot, TransportFlags flags)
{
  poll_events_[slot].events &= ~(POLLIN|POLLOUT);
  if (flags & kTransportReading)
    poll_events_[slot].events |= POLLIN;
  if (flags & kTransportWriting)
    poll_events_[slot].events |= POLLOUT;
}

template <int inFlag, TransportFlags outFlag>
inline void
PollImpl::handleEvent(size_t event_idx, int fd)
{
  Ref<PosixTransport> transport = fds_[fd].transport;
  if (transport->flags() & kTransportLT) {
    // Ignore - the event's been changed.
    if (!(transport->flags() & outFlag))
      return;
  } else {
    // Unset to emulate edge-triggered behavior.
    poll_events_[event_idx].events &= ~inFlag;
    transport->flags() &= ~outFlag;
  }

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

PassRef<IOError>
PollImpl::Poll(int timeoutMs)
{
  // We acquire a lock specifically for Poll(), to make sure it isn't called on
  // any thread or within callbacks.
  AutoMaybeLock poll_lock(poll_lock_);

  size_t poll_buffer_len;
  struct pollfd *poll_buffer;
  if (lock_) {
    // We need to the copy poll buffer; otherwise, we could mutate the buffer
    // while poll() is operating, and locking over poll() could trivially lead
    // to deadlocks.
    AutoMaybeLock lock(lock_);

    if (tmp_buffer_len_ < poll_events_.length()) {
      struct pollfd *new_buffer = new struct pollfd[poll_events_.length()];
      if (!new_buffer)
        return eOutOfMemory;
      tmp_buffer_ = new_buffer;
      tmp_buffer_len_ = poll_events_.length();
      memcpy(tmp_buffer_, poll_events_.buffer(), sizeof(struct pollfd) * tmp_buffer_len_);
    }

    poll_buffer = tmp_buffer_;
    poll_buffer_len = tmp_buffer_len_;
  } else {
    poll_buffer = poll_events_.buffer();
    poll_buffer_len = poll_events_.length();
  }

  int nevents = poll(poll_buffer, poll_buffer_len, timeoutMs);
  if (nevents == -1)
    return new PosixError();
  if (nevents == 0)
    return nullptr;

  // Now we acquire the transport lock.
  AutoMaybeLock lock(lock_);

  generation_++;
  for (size_t i = 0; i < poll_buffer_len && nevents > 0; i++) {
    int revents = poll_buffer[i].revents;
    if (revents == 0)
      continue;

    int fd = poll_buffer[i].fd;
    nevents--;

    // We have to check this in case the list changes during iteration.
    if (isFdChanged(fd))
      continue;

    // Handle errors first.
    if (revents & POLLERR) {
      reportError_locked(fds_[fd].transport);
      continue;
    }

    // Prioritize POLLIN over POLLHUP/POLLRDHUP.
    if (revents & POLLIN) {
      handleEvent<POLLIN, kTransportReading>(i, fd);
      if (isFdChanged(fd))
        continue;
    }

    // Handle explicit hangup.
#if defined(__linux__)
    if (revents & (POLLRDHUP|POLLHUP)) {
#else
    if (revents & POLLHUP) {
#endif
      reportHup_locked(fds_[fd].transport);
      continue;
    }

    // Handle output.
    if (revents & POLLOUT)
      handleEvent<POLLOUT, kTransportWriting>(i, fd);
  }

  return nullptr;
}

void
PollImpl::Interrupt()
{
  // NYI
  abort();
}
