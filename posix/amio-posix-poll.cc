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

PollMessagePump::PollMessagePump()
 : can_use_rdhup_(false),
   generation_(0)
{
#if defined(__linux__)
  if (IsAtLeastLinux(2, 6, 17))
    can_use_rdhup_ = true;
#endif
}

PollMessagePump::~PollMessagePump()
{
  for (size_t i = 0; i < pollfds_.length(); i++) {
    int fd = pollfds_[i].fd;
    if (fd == -1)
      continue;

    if (listeners_[fd].transport)
      listeners_[fd].transport->setPump(nullptr);
  }
}

Ref<IOError>
PollMessagePump::Initialize()
{
  if (!listeners_.resize(kInitialPollSize))
    return eOutOfMemory;
  return nullptr;
}

Ref<IOError>
PollMessagePump::Register(Ref<Transport> baseTransport, Ref<StatusListener> listener)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport)
    return eIncompatibleTransport;
  if (transport->pump())
    return eTransportAlreadyRegistered;
  if (transport->fd() == -1)
    return eTransportClosed;

  if (size_t(transport->fd()) >= listeners_.length()) {
    if (!listeners_.resize(transport->fd() + 1))
      return eOutOfMemory;
  }

  assert(listener);
  assert(size_t(transport->fd()) < listeners_.length());

  // By default we wait for reads (see the comment in the select pump).
  int defaultEvents = POLLIN | POLLERR | POLLHUP;
#if defined(__linux__)
  if (can_use_rdhup_)
    defaultEvents |= POLLRDHUP;
#endif

  size_t slot;
  struct pollfd pe;
  pe.fd = transport->fd();
  pe.events = defaultEvents;
  if (free_slots_.empty()) {
    slot = pollfds_.length();
    if (!pollfds_.append(pe))
      return eOutOfMemory;
  } else {
    slot = free_slots_.popCopy();
    pollfds_[slot] = pe;
  }

  transport->setPump(this);
  listeners_[transport->fd()].transport = transport;
  listeners_[transport->fd()].listener = listener;
  listeners_[transport->fd()].slot = slot;
  listeners_[transport->fd()].modified = generation_;
  return nullptr;
}

void
PollMessagePump::Deregister(Ref<Transport> baseTransport)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport || transport->pump() != this || transport->fd() == -1)
    return;

  unhook(transport);
}

Ref<IOError>
PollMessagePump::Poll(int timeoutMs)
{
  int nevents = poll(pollfds_.buffer(), pollfds_.length(), timeoutMs);
  if (nevents == -1)
    return new PosixError();
  if (nevents == 0)
    return nullptr;

  generation_++;
  for (size_t i = 0; i < pollfds_.length() && nevents > 0; i++) {
    int revents = pollfds_[i].revents;
    if (revents == 0)
      continue;

    int fd = pollfds_[i].fd;
    nevents--;

    // We have to check this in case the list changes during iteration.
    if (!isEventValid(fd))
      continue;

    // Handle errors first.
    if (revents & POLLERR) {
      // Get a local copy of the poll data before we wipe it out.
      PollData data = listeners_[fd];
      unhook(data.transport);
      data.listener->OnError(data.transport, eUnknownHangup);
      continue;
    }

    // Prioritize POLLIN over POLLHUP/POLLRDHUP.
    if (revents & POLLIN) {
      // Remove the flag to simulate edge-triggering.
      pollfds_[fd].events &= ~POLLIN;

      listeners_[fd].listener->OnReadReady(listeners_[fd].transport);
      if (!isEventValid(fd))
        continue;
    }

    // Handle explicit hangup.
#if defined(__linux__)
    if (revents & (POLLRDHUP|POLLHUP))
#else
    if (revents & POLLHUP)
#endif
    {
      // Get a local copy of the poll data before we wipe it out.
      PollData data = listeners_[fd];
      unhook(data.transport);
      data.listener->OnHangup(data.transport);
      continue;
    }

    // Handle output.
    if (revents & POLLOUT) {
      // Remove the flag to simulate edge-triggering.
      pollfds_[fd].events &= ~POLLOUT;

      // This is the last event we handle, so we don't need any re-entrancy
      // checks.
      listeners_[fd].listener->OnWriteReady(listeners_[fd].transport);
    }
  }

  return nullptr;
}

void
PollMessagePump::Interrupt()
{
  // Not yet implemented.
  abort();
}

void
PollMessagePump::onReadWouldBlock(int fd)
{
  size_t slot = listeners_[fd].slot;
  assert(pollfds_[slot].fd == fd);

  pollfds_[slot].events |= POLLIN;
}

void
PollMessagePump::onWriteWouldBlock(int fd)
{
  size_t slot = listeners_[fd].slot;
  assert(pollfds_[slot].fd == fd);

  pollfds_[slot].events |= POLLOUT;
}

void
PollMessagePump::unhook(Ref<PosixTransport> transport)
{
  int fd = transport->fd();
  assert(fd != -1);
  assert(listeners_[fd].transport == transport);

  size_t slot = listeners_[fd].slot;
  assert(pollfds_[slot].fd == fd);

  pollfds_[slot].fd = -1;
  listeners_[fd].transport = nullptr;
  listeners_[fd].listener = nullptr;
  listeners_[fd].modified = generation_;
  free_slots_.append(slot);

  transport->setPump(nullptr);
}
