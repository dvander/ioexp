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

#if defined(__linux__) && !defined(POLLRDHUP)
# define POLLRDHUP 0x2000
#endif

using namespace ke;
using namespace amio;

static const size_t kInitialPollSize = 4096;

PollImpl::PollImpl()
 : generation_(0)
{
#if defined(__linux__)
  can_use_rdhup_ = false;
  if (IsAtLeastLinux(2, 6, 17))
    can_use_rdhup_ = true;
#endif
}

PollImpl::~PollImpl()
{
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
PollImpl::Attach(Ref<Transport> baseTransport, Ref<StatusListener> listener, EventFlags eventMask)
{
  PosixTransport *transport;
  Ref<IOError> error = toPosixTransport(&transport, baseTransport);
  if (error)
    return error;

  if (size_t(transport->fd()) >= fds_.length()) {
    if (!fds_.resize(transport->fd() + 1))
      return eOutOfMemory;
  }

  assert(listener);
  assert(size_t(transport->fd()) < fds_.length());

  // By default we wait for reads (see the comment in the select pump).
  int defaultEvents = POLLERR | POLLHUP;
#if defined(__linux__)
  if (can_use_rdhup_)
    defaultEvents |= POLLRDHUP;
#endif

  if (eventMask & Event_Read)
    defaultEvents |= POLLIN;
  if (eventMask & Event_Write)
    defaultEvents |= POLLOUT;

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

  transport->attach(this, listener);
  fds_[transport->fd()].transport = transport;
  fds_[transport->fd()].slot = slot;
  fds_[transport->fd()].modified = generation_;
  fds_[transport->fd()].flags = eventMask;
  return nullptr;
}

PassRef<IOError>
PollImpl::ChangeStickyEvents(Ref<Transport> baseTransport, EventFlags eventMask)
{
  Ref<PosixTransport> transport = validateEventChange(baseTransport, eventMask);
  if (!transport)
    return eIncompatibleTransport;

  int fd = transport->fd();
  if (!(fds_[fd].flags & Event_Sticky))
    return eIncompatibleTransport;

  size_t slot = fds_[fd].slot;
  poll_events_[slot].events &= ~(POLLIN|POLLOUT);
  if (eventMask & Event_Read)
    poll_events_[slot].events |= POLLIN;
  if (eventMask & Event_Write)
    poll_events_[slot].events |= POLLOUT;

  fds_[fd].flags = eventMask;
  return nullptr;
}

void
PollImpl::Detach(Ref<Transport> baseTransport)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport || transport->pump() != this || transport->fd() == -1)
    return;

  unhook(transport);
}

template <int inFlag, EventFlags outFlag>
inline void
PollImpl::handleEvent(size_t event_idx, int fd)
{
  if (fds_[fd].flags & Event_Sticky) {
    // Ignore - the event's been changed.
    if (!(fds_[fd].flags & outFlag))
      return;
  } else {
    // Unset to emulate edge-triggered behavior.
    poll_events_[event_idx].events &= ~inFlag;
  }

  if (outFlag == Event_Read)
    fds_[fd].transport->listener()->OnReadReady(fds_[fd].transport);
  else if (outFlag == Event_Write)
    fds_[fd].transport->listener()->OnWriteReady(fds_[fd].transport);
}

PassRef<IOError>
PollImpl::Poll(int timeoutMs)
{
  int nevents = poll(poll_events_.buffer(), poll_events_.length(), timeoutMs);
  if (nevents == -1)
    return new PosixError();
  if (nevents == 0)
    return nullptr;

  generation_++;
  for (size_t i = 0; i < poll_events_.length() && nevents > 0; i++) {
    int revents = poll_events_[i].revents;
    if (revents == 0)
      continue;

    int fd = poll_events_[i].fd;
    nevents--;

    // We have to check this in case the list changes during iteration.
    if (isFdChanged(fd))
      continue;

    // Handle errors first.
    if (revents & POLLERR) {
      reportError(fds_[fd].transport);
      continue;
    }

    // Prioritize POLLIN over POLLHUP/POLLRDHUP.
    if (revents & POLLIN) {
      handleEvent<POLLIN, Event_Read>(i, fd);
      if (isFdChanged(fd))
        continue;
    }

    // Handle explicit hangup.
#if defined(__linux__)
    if (revents & (POLLRDHUP|POLLHUP)) {
#else
    if (revents & POLLHUP) {
#endif
      reportHup(fds_[fd].transport);
      continue;
    }

    // Handle output.
    if (revents & POLLOUT)
      handleEvent<POLLOUT, Event_Write>(i, fd);
  }

  return nullptr;
}

void
PollImpl::Interrupt()
{
  // NYI
  abort();
}

PassRef<IOError>
PollImpl::onReadWouldBlock(PosixTransport *transport)
{
  int fd = transport->fd();
  size_t slot = fds_[fd].slot;
  assert(poll_events_[slot].fd == fd);

  poll_events_[slot].events |= POLLIN;
  return nullptr;
}

PassRef<IOError>
PollImpl::onWriteWouldBlock(PosixTransport *transport)
{
  int fd = transport->fd();
  size_t slot = fds_[fd].slot;
  assert(poll_events_[slot].fd == fd);

  poll_events_[slot].events |= POLLOUT;
  return nullptr;
}

void
PollImpl::unhook(PosixTransport *transport)
{
  int fd = transport->fd();
  assert(fd != -1);
  assert(fds_[fd].transport == transport);
  assert(transport->pump() == this);

  size_t slot = fds_[fd].slot;
  assert(poll_events_[slot].fd == fd);

  // Note: just for safety, we call this after we're done with the transport,
  // in case the assignment below drops the last ref.
  transport->detach();

  poll_events_[slot].fd = -1;
  fds_[fd].transport = nullptr;
  fds_[fd].modified = generation_;
  free_slots_.append(slot);
}
