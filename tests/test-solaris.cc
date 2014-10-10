// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include <amio.h>
#include "posix/test-pipes.h"

using namespace ke;
using namespace amio;

static PassRef<IOError>
create_devpoll(Ref<Poller> *outp)
{
  return PollerFactory::CreateDevPollImpl(outp);
}

static PassRef<IOError>
create_port(Ref<Poller> *outp)
{
  return PollerFactory::CreateCompletionPort(outp);
}

void
ke::SetupTests()
{
  Tests.append(new TestPipes(PollerFactory::CreateSelectImpl, "select-pipe"));
  Tests.append(new TestPipes(PollerFactory::CreatePollImpl, "poll-pipe"));
  Tests.append(new TestPipes(create_devpoll, "/dev/poll-pipe"));
  Tests.append(new TestPipes(create_port, "ioport-pipe"));
}
