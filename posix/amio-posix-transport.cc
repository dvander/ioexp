// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "posix/amio-posix-transport.h"
#include "posix/amio-posix-errors.h"
#include "posix/amio-posix-base-poller.h"
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

using namespace amio;

PosixTransport::PosixTransport(int fd, TransportFlags flags)
 : fd_(fd),
   flags_(flags),
   pump_(nullptr)
{
}

PosixTransport::~PosixTransport()
{
  if (!(flags_ & kTransportNoAutoClose))
    Close();
}

void
PosixTransport::Close()
{
  if (fd_ == -1) {
    assert(!pump_ && !listener_);
    return;
  }

  if (pump_)
    pump_->unhook(this);

  close(fd_);
  fd_ = -1;

  assert(!pump_ && !listener_);
}

bool
PosixTransport::Read(IOResult *result, void *buffer, size_t maxlength)
{
  *result = IOResult();

  ssize_t rv = read(fd_, buffer, maxlength);
  if (rv == -1) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      if (pump_)
        pump_->onReadWouldBlock(this);
      return true;
    }

    result->Error = new PosixError();
    return false;
  }

  result->Completed = true;
  if (rv == 0) {
    pump_->unhook(this);
    result->Ended = true;
    return true;
  }

  result->Bytes = size_t(rv);
  return true;
}

bool
PosixTransport::Write(IOResult *result, const void *buffer, size_t maxlength)
{
  *result = IOResult();

  ssize_t rv = write(fd_, buffer, maxlength);
  if (rv == -1) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      if (pump_)
        pump_->onWriteWouldBlock(this);
      return true;
    }

    result->Error = new PosixError();
    return false;
  }

  result->Completed = true;
  result->Bytes = size_t(rv);
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
