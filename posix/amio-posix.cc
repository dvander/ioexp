// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "include/amio.h"
#include "posix/amio-posix-transport.h"
#include "posix/amio-posix-errors.h"
#include "posix/amio-posix-select.h"
#if defined(AMIO_POLL_AVAILABLE)
# include "posix/amio-posix-poll.h"
#endif
#include <unistd.h>
#include <fcntl.h>

using namespace ke;
using namespace amio;

static Ref<IOError>
SetNonBlocking(int fd)
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

PassRef<IOError>
TransportFactory::CreateFromDescriptor(Ref<Transport> *outp, int fd, TransportFlags flags)
{
  Ref<IOError> error = SetNonBlocking(fd);
  if (error)
    return error;
  *outp = new PosixTransport(fd, flags);
  return nullptr;
}

PassRef<IOError>
TransportFactory::CreatePipe(Ref<Transport> *readerp, Ref<Transport> *writerp)
{
  int fds[2];
  if (pipe(fds) == -1)
    return new PosixError();

  Ref<IOError> error;
  if (((error = SetNonBlocking(fds[0])) != nullptr) ||
       (error = SetNonBlocking(fds[1])) != nullptr)
  {
    close(fds[0]);
    close(fds[1]);
    return error;
  }

  *readerp = new PosixTransport(fds[0], kTransportDefaultFlags);
  *writerp = new PosixTransport(fds[1], kTransportDefaultFlags);
  return nullptr;
}

PassRef<IOError>
PollerFactory::CreateSelectImpl(Poller **outp)
{
  *outp = new SelectImpl();
  return nullptr;
}

#if defined(AMIO_POLL_AVAILABLE)
PassRef<IOError>
PollerFactory::CreatePollImpl(Poller **outp)
{
  AutoPtr<PollImpl> poller(new PollImpl());
  Ref<IOError> error = poller->Initialize();
  if (error)
    return error;
  *outp = poller.take();
  return nullptr;
}
#endif
