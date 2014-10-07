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
#include "solaris/amio-solaris-devpoll.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/devpoll.h>
#include <stropts.h>

using namespace ke;
using namespace amio;

static const size_t kInitialPollSize = 1024;
static Ref<GenericError> sDevPollWriteFailed = new GenericError("write to /dev/poll did not complete");

DevPollImpl::DevPollImpl()
 : dp_(-1),
   generation_(0),
   max_events_(0)
{
}

PassRef<IOError>
DevPollImpl::Initialize(size_t maxEventsPerPoll)
{
  if ((dp_ = open("/dev/poll", O_RDWR)) == -1)
    return new PosixError();

  if (maxEventsPerPoll == 0)
    maxEventsPerPoll = kDefaultMaxEventsPerPoll; 

  event_buffer_ = new struct pollfd[maxEventsPerPoll];
  if (!event_buffer_)
    return eOutOfMemory;
  max_events_ = maxEventsPerPoll;

  if (!fds_.resize(kInitialPollSize))
    return eOutOfMemory;

  return nullptr;
}

DevPollImpl::~DevPollImpl()
{
  if (dp_ == -1)
    return;

  for (size_t i = 0; i < fds_.length(); i++) {
    if (fds_[i].transport)
      fds_[i].transport->detach();
  }

  close(dp_);
}

static inline PassRef<IOError>
WriteDevPoll(int dp, struct pollfd *pe)
{
  int rv = write(dp, pe, sizeof(*pe));
  if (rv == -1)
    return new PosixError();
  if (size_t(rv) != sizeof(*pe))
    return sDevPollWriteFailed;
  return nullptr;
}

PassRef<IOError>
DevPollImpl::Attach(Ref<Transport> baseTransport, Ref<StatusListener> listener, EventFlags eventMask)
{
  PosixTransport *transport;
  Ref<IOError> error = toPosixTransport(&transport, baseTransport);
  if (error)
    return error;

  assert(listener);

  if (size_t(transport->fd()) >= fds_.length()) {
    if (!fds_.resize(size_t(transport->fd())))
      return eOutOfMemory;
  }

  // Hook up events.
  struct pollfd pe;
  pe.fd = transport->fd();
  pe.events = 0;
  if (eventMask & Event_Read)
    pe.events |= POLLIN;
  if (eventMask & Event_Write)
    pe.events |= POLLOUT;
  pe.revents = 0;

  Ref<IOError> error = WriteDevPoll(dp_, &pe);
  if (error)
    return error;

  // Hook up the transport.
  fds_[transport->fd()].transport = transport;
  fds_[transport->fd()].modified = generation_;
  fds_[transport->fd()].pe = pe;
  fds_[transport->fd()].stickied = 0;
  transport->attach(this, listener);
  return nullptr;
}

void
DevPollImpl::Detach(Ref<Transport> baseTransport)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport || transport->pump() != this || transport->fd() == -1)
    return;

  unhook(transport);
}

PassRef<IOError>
DevPollImpl::Poll(int timeoutMs)
{
  struct dvpoll params;
  params.dp_fds = event_buffer_;
  params.dp_nfds = max_events_;
  params.dp_timeout = timeoutMs;

  int nevents = ioctl(dp_, DP_POLL, &params);
  if (nevents == -1)
    return new PosixError();

  generation_++;
  for (int i = 0; i < nevents; i++) {
    struct pollfd &pe = event_buffer_[i];
    int slot = pe.fd;
    if (!isEventValid(slot))
      continue;

    // Handle errors first.
    if (pe.revents & POLLERR) {
      Ref<PosixTransport> transport = fds_[slot].transport;
      Ref<StatusListener> listener = transport->listener();
      unhook(transport);
      listener->OnError(transport, eUnknownHangup);
      continue;
    }

    // Prioritize POLLIN over POLLHUP
    if ((pe.events & POLLIN) && !(fds_[slot].stickied & POLLIN)) {
      fds_[slot].stickied |= POLLIN;
      fds_[slot].transport->listener()->OnReadReady(fds_[slot].transport);
      if (!isEventValid(slot))
        continue;
    }

    // Handle explicit hangup.
    if (pe.events & POLLHUP) {
      Ref<PosixTransport> transport = fds_[slot].transport;
      Ref<StatusListener> listener = transport->listener();
      unhook(transport);
      listener->OnHangup(transport);
      continue;
    }

    // Handle output.
    if ((pe.events & POLLOUT) && !(fds_[slot].stickied & POLLOUT)) {
      // No need to check if the event is still valid since this is the last
      // check.
      fds_[slot].stickied |= POLLOUT;
      fds_[slot].transport->listener()->OnWriteReady(fds_[slot].transport);
    }
  }

  return nullptr;
}

void
DevPollImpl::Interrupt()
{
  // Not yet implemented.
  abort();
}

PassRef<IOError>
DevPollImpl::addEventFlag(int fd, int flag)
{
  Ref<IOError> error;

  struct pollfd tmp = fds_[fd].pe;
  tmp.events = POLLREMOVE;
  if ((error = WriteDevPoll(dp_, &tmp)) != nullptr)
    return error;

  fds_[fd].pe.events |= flag;
  if ((error = WriteDevPoll(dp_, &fds_[fd].pe)) != nullptr) {
    fds_[fd].pe.events &= ~flag;
    return error;
  }

  return nullptr;
}

PassRef<IOError>
DevPollImpl::onReadWouldBlock(PosixTransport *transport)
{
  int fd = transport->fd();
  fds_[fd].stickied &= ~POLLIN;
  if (!(fds_[fd].pe.events & POLLIN))
    return addEventFlag(fd, POLLIN);
  return nullptr;
}

PassRef<IOError>
DevPollImpl::onWriteWouldBlock(PosixTransport *transport)
{
  int fd = transport->fd();
  fds_[fd].stickied &= ~POLLOUT;
  if (!(fds_[fd].pe.events & POLLOUT))
    return addEventFlag(fd, POLLOUT);
  return nullptr;
}

void
DevPollImpl::unhook(PosixTransport *transport)
{
  assert(transport->pump() == this);

  int fd = transport->fd();
  assert(fd != -1);
  assert(fds_[fd].transport == transport);

  fds_[fd].pe.events = POLLREMOVE;
  WriteDevPoll(dp_, &fds_[fd].pe);

  // Just for safety, we detach here in case the assignment below drops the
  // last ref to the transport.
  transport->detach();

  fds_[fd].transport = nullptr;
  fds_[fd].modified = generation_;
}
