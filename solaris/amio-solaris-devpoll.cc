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
   generation_(0)
{
}

PassRef<IOError>
DevPollImpl::Initialize(size_t maxEventsPerPoll)
{
  if ((dp_ = open("/dev/poll", O_RDWR)) == -1)
    return new PosixError();

  if (!event_buffer_.init(32, maxEventsPerPoll))
    return eOutOfMemory;

  if (!fds_.resize(kInitialPollSize))
    return eOutOfMemory;

  return nullptr;
}

DevPollImpl::~DevPollImpl()
{
  Shutdown();
}

void
DevPollImpl::Shutdown()
{
  AutoMaybeLock lock(lock_);

  if (dp_ == -1)
    return;

  for (size_t i = 0; i < fds_.length(); i++) {
    if (fds_[i].transport)
      fds_[i].transport->detach();
  }

  AMIO_RETRY_IF_EINTR(close(dp_));
}

static inline PassRef<IOError>
WriteDevPoll(int dp, int fd, TransportFlags flags)
{
  struct pollfd pe;
  pe.fd = fd;
  pe.events = 0;
  if (flags & kTransportEventMask) {
    if (flags & kTransportReading)
      pe.events |= POLLIN;
    if (flags & kTransportWriting)
      pe.events |= POLLOUT;
  } else {
    pe.events = POLLREMOVE;
  }

  int rv = AMIO_RETRY_IF_EINTR(write(dp, &pe, sizeof(pe)));
  if (rv == -1)
    return new PosixError();
  if (size_t(rv) != sizeof(pe))
    return sDevPollWriteFailed;
  return nullptr;
}

PassRef<IOError>
DevPollImpl::attach_locked(PosixTransport *transport, StatusListener *listener, TransportFlags flags)
{
  if (size_t(transport->fd()) >= fds_.length()) {
    if (!fds_.resize(size_t(transport->fd())))
      return eOutOfMemory;
  }

  if (flags & kTransportEventMask) {
    if (Ref<IOError> error = WriteDevPoll(dp_, transport->fd(), flags))
      return error;
  }

  // Hook up the transport.
  fds_[transport->fd()].transport = transport;
  fds_[transport->fd()].modified = generation_;
  transport->attach(this, listener);
  transport->flags() |= flags;
  return nullptr;
}

void
DevPollImpl::detach_locked(PosixTransport *transport)
{
  int fd = transport->fd();
  assert(fd != -1);
  assert(fds_[fd].transport == transport);

  WriteDevPoll(dp_, fd, kTransportNoFlags);

  // Just for safety, we detach here in case the assignment below drops the
  // last ref to the transport.
  transport->detach();

  fds_[fd].transport = nullptr;
  fds_[fd].modified = generation_;
}

PassRef<IOError>
DevPollImpl::change_events_locked(PosixTransport *transport, TransportFlags flags)
{
  // Is this needed?
  if (Ref<IOError> error = WriteDevPoll(dp_, transport->fd(), kTransportNoFlags))
    return error;
  transport->flags() &= ~kTransportEventMask;

  if (Ref<IOError> error = WriteDevPoll(dp_, transport->fd(), flags))
    return error;
  transport->flags() |= flags;

  return nullptr;
}

template <TransportFlags inFlag>
inline void
DevPollImpl::handleEvent(int fd)
{
  Ref<PosixTransport> transport = fds_[fd].transport;

  // Skip if we don't want this event at all.
  if (!(transport->flags() & inFlag))
    return;

  // If we're edge-triggered, remove this flag. It is fairly complex to add
  // the flag after we call the handler, since the user may have removed it.
  // Instead we just add it back immediately even if this means slightly more
  // calls to write().
  if (transport->flags() & kTransportET) {
    if (Ref<IOError> error = rm_events_locked(transport, inFlag)) {
      reportError_locked(transport, error);
      return;
    }
  }

  // We must hold the listener in a ref, since if the transport is detached
  // in the callback, it could be destroyed while |this| is still on the
  // stack. Similarly, we must hold the transport in a ref in case releasing
  // the lock allows a detach to happen.
  Ref<StatusListener> listener = transport->listener();

  AutoMaybeUnlock unlock(lock_);

  if (inFlag == kTransportReading)
    listener->OnReadReady(transport);
  else if (inFlag == kTransportWriting)
    listener->OnWriteReady(transport);
}

PassRef<IOError>
DevPollImpl::Poll(int timeoutMs)
{
  AutoMaybeLock poll_lock(poll_lock_);

  struct dvpoll params;
  params.dp_fds = event_buffer_.get();
  params.dp_nfds = event_buffer_.length();
  params.dp_timeout = timeoutMs;

  int nevents = AMIO_RETRY_IF_EINTR(ioctl(dp_, DP_POLL, &params));
  if (nevents == -1)
    return new PosixError();

  AutoMaybeLock lock(lock_);

  generation_++;
  for (int i = 0; i < nevents; i++) {
    struct pollfd &pe = event_buffer_[i];
    int slot = pe.fd;
    if (isFdChanged(slot))
      continue;

    // Handle errors first.
    if (pe.revents & POLLERR) {
      reportError_locked(fds_[slot].transport);
      continue;
    }

    // Prioritize POLLIN over POLLHUP
    if (pe.events & POLLIN) {
      handleEvent<kTransportReading>(slot);
      if (isFdChanged(slot))
        continue;
    }

    // Handle explicit hangup.
    if (pe.events & POLLHUP) {
      reportHup_locked(fds_[slot].transport);
      continue;
    }

    // Handle output.
    if (pe.events & POLLOUT)
      handleEvent<kTransportWriting>(slot);
  }

  if (size_t(nevents) == event_buffer_.length()) {
    AutoMaybeUnlock unlock(lock_);
    event_buffer_.maybeResize();
  }

  return nullptr;
}
