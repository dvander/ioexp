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
#include <signal.h>

using namespace ke;
using namespace amio;

PassRef<IOError>
TransportFactory::CreateFromDescriptor(Ref<Transport> *outp, int fd, TransportFlags flags)
{
  Ref<IOError> error;
  Ref<PosixTransport> transport = new PosixTransport(fd, flags);
  if ((error = transport->Setup()) != nullptr)
    return error;
  *outp = transport;
  return nullptr;
}

PassRef<IOError>
TransportFactory::CreatePipe(Ref<Transport> *readerp, Ref<Transport> *writerp, TransportFlags flags)
{
  int fds[2];
  if (pipe(fds) == -1)
    return new PosixError();

  Ref<PosixTransport> reader = new PosixTransport(fds[0], kTransportDefaultFlags);
  if (Ref<IOError> error = reader->Setup())
    return error;

  Ref<PosixTransport> writer = new PosixTransport(fds[1], kTransportDefaultFlags);
  if (Ref<IOError> error = writer->Setup())
    return error;

  *readerp = reader;
  *writerp = writer;
  return nullptr;
}

PassRef<IOError>
PollerFactory::CreateSelectImpl(Ref<Poller> *outp)
{
  *outp = new SelectImpl();
  return nullptr;
}

#if defined(AMIO_POLL_AVAILABLE)
PassRef<IOError>
PollerFactory::CreatePollImpl(Ref<Poller> *outp)
{
  Ref<PollImpl> poller(new PollImpl());
  Ref<IOError> error = poller->Initialize();
  if (error)
    return error;
  *outp = poller;
  return nullptr;
}
#endif

AutoDisableSigPipe::AutoDisableSigPipe()
{
  prev_handler_ = signal(SIGPIPE, SIG_IGN);
}

AutoDisableSigPipe::~AutoDisableSigPipe()
{
  signal(SIGPIPE, prev_handler_);
}
