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
#include "linux/amio-linux.h"
#include "linux/amio-linux-poll.h"
#include <sys/poll.h>

using namespace ke;
using namespace amio;

static const size_t kInitialPollSize = 4096;

PollMessagePump::PollMessagePump()
 : can_use_rdhup_(false)
{
  int major, minor, release;
  if (GetLinuxVersion(&major, &minor, &release) &&
      ((major > 2 ||
       (minor == 2 && minor > 6) ||
       (minor == 2 && minor == 6 && release >= 17))))
  {
    can_use_rdhup_ = true;
  }
}

PollMessagePump::~PollMessagePump()
{
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
  if (can_use_rdhup_)
    defaultEvents |= POLLRDHUP;

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

  listeners_[transport->fd()].transport = transport;
  listeners_[transport->fd()].listener = listener;
  listeners_[transport->fd()].slot = slot;
  return nullptr;
}

void
PollMessagePump::Deregister(Ref<Transport> baseTransport)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (transport->fd() == -1)
    return;

  onClose(transport->fd());
}

Ref<IOError>
PollMessagePump::Poll(int timeoutMs)
{
  int nevents = poll(pollfds_.buffer(), pollfds_.length(), timeoutMs);
  if (nevents == -1)
    return new PosixError();
  if (nevents == 0)
    return nullptr;

  for (size_t i = 0; i < pollfds_.length() && nevents > 0; i++) {
    int revents = pollfds_[i].revents;
    if (revents == 0)
      continue;

    int fd = pollfds_[i].fd;
    nevents--;

    // We have to check this in case the list changes during iteration.
    if (size_t(fd) >= listeners_.length() || !listeners_[fd].transport)
      continue;

    if (revents & POLLRDHUP) {
      Ref<Transport> transport = listeners_[fd].transport;
      Ref<StatusListener> listener = listeners_[fd].listener;
      onClose(fd);
      listener->OnHangup(transport);
    }
    if (revents & POLLHUP) {
      Ref<Transport> transport = listeners_[fd].transport;
      Ref<StatusListener> listener = listeners_[fd].listener;
      onClose(fd);
      listener->OnError(transport, eUnknownHangup);
      continue;
    }
    if (revents & POLLIN) {
      listeners_[fd].listener->OnReadReady(listeners_[fd].transport);
      if (!listeners_[fd].transport)
        continue;
    }
    if (revents & POLLOUT) {
      pollfds_[fd].events &= ~POLLOUT;
      listeners_[fd].listener->OnWriteReady(listeners_[fd].transport);
      if (!listeners_[fd].transport)
        continue;
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
PollMessagePump::onClose(int fd)
{
  if (!listeners_[fd].transport || listeners_[fd].transport->pump() != this)
    return;
  size_t slot = listeners_[fd].slot;
  assert(pollfds_[slot].fd == fd);

  pollfds_[slot].fd = -1;
  listeners_[fd].transport = nullptr;
  listeners_[fd].listener = nullptr;
  free_slots_.append(slot);
}
