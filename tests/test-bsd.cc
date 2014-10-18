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
#include "common/test-server-client.h"

using namespace ke;
using namespace amio;

static PassRef<IOError>
create_kqueue(Ref<Poller> *outp)
{
  return PollerFactory::CreateKqueueImpl(outp);
}

void
ke::SetupTests()
{
  Tests.append(new TestPipes(PollerFactory::CreateSelectImpl, "select-pipe"));
  Tests.append(new TestPipes(PollerFactory::CreatePollImpl, "poll-pipe"));
  Tests.append(new TestPipes(create_kqueue, "kqueue-pipe"));

  Tests.append(new TestServerClient(PollerFactory::CreateSelectImpl, "select-server-client"));
  Tests.append(new TestServerClient(PollerFactory::CreatePollImpl, "poll-server-client"));
  Tests.append(new TestServerClient(create_kqueue, "kqueue-server-client"));
}
