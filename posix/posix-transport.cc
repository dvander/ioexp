// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "posix/posix-transport.h"
#include "posix/posix-errors.h"
#include "posix/posix-base-poller.h"
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

using namespace amio;

PosixTransport::PosixTransport(int fd, TransportFlags flags)
 : fd_(fd),
   flags_(flags & kTransportUserFlagMask)
{
}

PosixTransport::~PosixTransport()
{
  assert(!poller_.get());

  if (!(flags_ & kTransportNoAutoClose))
    Close();
}

void
PosixTransport::Close()
{
  if (fd_ == -1) {
    assert(!poller_.get() && !listener_);
    return;
  }

  if (Ref<PosixPoller> poller = poller_.get()) {
    // If this succeeds, we are guarateed to not be in a destructor. We could
    // reach Close() through the destructor, but only if no pump is attached
    // (since that would mean a reference is alive). Therefore we can call
    // Detach() here without worrying that we'll add back a reference to a
    // dead object.
    //
    // Note, even if the poller detaches in between grabbing the local, we're
    // still not in a destructor because the pump was attached when we
    // grabbed it.
    poller->detach_unlocked(this);
  }

  close(fd_);
  fd_ = -1;

  assert(!poller_.get() && !listener_);
}

bool
PosixTransport::Read(IOResult *result, void *buffer, size_t maxlength)
{
  *result = IOResult();

  ssize_t rv = read(fd_, buffer, maxlength);
  if (rv == -1) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      if (Ref<IOError> error = ReadIsBlocked()) {
        result->error = error;
        return false;
      }
      return true;
    }

    result->error = new PosixError();
    return false;
  }

  result->completed = true;
  if (rv == 0) {
    result->ended = true;
    return true;
  }

  result->bytes = size_t(rv);
  return true;
}

bool
PosixTransport::Write(IOResult *result, const void *buffer, size_t maxlength)
{
  *result = IOResult();

  ssize_t rv = write(fd_, buffer, maxlength);
  if (rv == -1) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      if (Ref<IOError> error = WriteIsBlocked()) {
        result->error = error;
        return false;
      }
      return true;
    }

    result->error = new PosixError();
    return false;
  }

  result->completed = true;
  result->bytes = size_t(rv);
  return true;
}

static Ref<IOError>
SetNonblocking(int fd)
{
  int flags = fcntl(fd, F_GETFL);
  if (flags == -1)
    return new PosixError();
  if (!(flags & O_NONBLOCK)) {
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
      return new PosixError();
  }
  return nullptr;
}

static Ref<IOError>
SetCloseOnExec(int fd)
{
  int flags = fcntl(fd, F_GETFD);
  if (flags == -1)
    return new PosixError();
  if (!(flags & FD_CLOEXEC)) {
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
      return new PosixError();
  }
  return nullptr;
}

PassRef<IOError>
PosixTransport::Setup()
{
  Ref<IOError> error;
  if ((error = SetNonblocking(fd_)))
    return error;
  if (!(flags_ & kTransportNoCloseOnExec)) {
    if ((error = SetCloseOnExec(fd_)))
      return error;
  }
  return nullptr;
}

PassRef<IOError>
PosixTransport::ReadIsBlocked()
{
  // Note: we don't acquire the lock here. This is okay, the user is responsible
  // for synchronizing event delivery if needed.
  if (!(flags_ & kTransportReading)) {
    if (Ref<PosixPoller> poller = poller_.get()) {
      if (Ref<IOError> error = poller->add_events_unlocked(this, kTransportReading))
        return error;
    }
  }
  return nullptr;
}

PassRef<IOError>
PosixTransport::WriteIsBlocked()
{
  // Note: we don't acquire the lock here. This is okay, the user is responsible
  // for synchronizing event delivery if needed.
  if (!(flags_ & kTransportWriting)) {
    if (Ref<PosixPoller> poller = poller_.get()) {
      if (Ref<IOError> error = poller->add_events_unlocked(this, kTransportWriting))
        return error;
    }
  }
  return nullptr;
}

// This is only ever called from Attach() in a poller, where we're
// guaranteed to have live references.
void
PosixTransport::attach(PosixPoller *poller, StatusListener *listener)
{
  // Do not overwrite an existing pump with a non-null pump.
  poller_ = poller;
  listener_ = listener;
}

PassRef<StatusListener>
PosixTransport::detach()
{
  poller_ = nullptr;
  flags_ &= ~kTransportClearMask;
  return listener_.take();
}

void
PosixTransport::changeListener(Ref<StatusListener> listener)
{
  assert(listener_);
  assert(listener);

  if (isProxying())
    listener_->OnChangeProxy(listener);
  else
    listener_ = listener;
}
