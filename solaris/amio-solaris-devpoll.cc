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

static const int kEdgeTriggered = 0x800000;
static const size_t kInitialPollSize = 1024;
static Ref<GenericError> sDevPollWriteFailed = new GenericError("write to /dev/poll did not complete");

DevPollImpl::DevPollImpl()
 : dp_(-1),
   generation_(0),
   max_events_(0)
{
  assert((kEdgeTriggered & (POLLIN|POLLOUT)) == 0);
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

  if (Ref<IOError> error = WriteDevPoll(dp_, &pe))
    return error;

  // Hook up the transport.
  fds_[transport->fd()].transport = transport;
  fds_[transport->fd()].modified = generation_;
  fds_[transport->fd()].pe = pe;
  fds_[transport->fd()].flags = (eventMask & Event_Sticky) ? 0 : kEdgeTriggered;
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
DevPollImpl::ChangeStickyEvents(Ref<Transport> baseTransport, EventFlags eventMask)
{
  Ref<PosixTransport> transport = validateEventChange(baseTransport, eventMask);
  if (!transport)
    return eIncompatibleTransport;

  int fd = transport->fd();
  if (fds_[fd].flags & kEdgeTriggered)
    return eIncompatibleTransport;

  int events = 0;
  if (eventMask & Event_Read)
    events |= POLLIN;
  if (eventMask & Event_Write)
    events |= POLLOUT;
  if (events == fds_[fd].pe.events)
    return nullptr;

  struct pollfd tmp = fds_[fd].pe;
  tmp.events = POLLREMOVE;
  if (Ref<IOError> error = WriteDevPoll(dp_, &tmp))
    return error;

  fds_[fd].pe.events = events;
  if (Ref<IOError> error = WriteDevPoll(dp_, &fds_[fd].pe)) {
    fds_[fd].pe.events = 0;
    return error;
  }

  return nullptr;
}

template <int inFlag>
inline void
DevPollImpl::handleEvent(int fd)
{
  // Skip if we're edge triggered and have already seen this event.
  if ((fds_[fd].flags & (inFlag|kEdgeTriggered)) == (inFlag|kEdgeTriggered))
    return;

  // Skip if we don't want this event at all.
  if (!(fds_[fd].pe.events & inFlag))
    return;

  fds_[fd].flags |= inFlag;

  if (inFlag == POLLIN)
    fds_[fd].transport->listener()->OnReadReady(fds_[fd].transport);
  else if (inFlag == POLLOUT)
    fds_[fd].transport->listener()->OnWriteReady(fds_[fd].transport);
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
    if (isFdChanged(slot))
      continue;

    // Handle errors first.
    if (pe.revents & POLLERR) {
      reportError(fds_[slot].transport);
      continue;
    }

    // Prioritize POLLIN over POLLHUP
    if (pe.events & POLLIN) {
      handleEvent<POLLIN>(slot);
      if (isFdChanged(slot))
        continue;
    }

    // Handle explicit hangup.
    if (pe.events & POLLHUP) {
      reportHup(fds_[slot].transport);
      continue;
    }

    // Handle output.
    if (pe.events & POLLOUT)
      handleEvent<POLLOUT>(slot);
  }

  return nullptr;
}

void
DevPollImpl::Interrupt()
{
  // Not yet implemented.
  abort();
}

template <int inFlag>
inline PassRef<IOError>
DevPollImpl::addEventFlag(PosixTransport *transport)
{
  int fd = transport->fd();

  // Unset the flag so we'll receive it again
  fds_[fd].flags &= ~inFlag;

  // Check if we need to watch a new event.
  if (!(fds_[fd].pe.events & inFlag)) {
    struct pollfd tmp = fds_[fd].pe;
    tmp.events = POLLREMOVE;
    if (Ref<IOError> error = WriteDevPoll(dp_, &tmp))
      return error;

    fds_[fd].pe.events |= inFlag;
    if (Ref<IOError> error = WriteDevPoll(dp_, &fds_[fd].pe)) {
      fds_[fd].pe.events &= ~inFlag;
      return error;
    }
  }
  return nullptr;
}

PassRef<IOError>
DevPollImpl::onReadWouldBlock(PosixTransport *transport)
{
  return addEventFlag<POLLIN>(transport);
}

PassRef<IOError>
DevPollImpl::onWriteWouldBlock(PosixTransport *transport)
{
  return addEventFlag<POLLOUT>(transport);
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
