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
#include "common/test-server-client.h"

using namespace ke;
using namespace amio;

static PassRef<IOError>
create_port(Ref<Poller> *outp)
{
  return PollerFactory::CreateCompletionPort(outp, 1);
}

void
ke::SetupTests()
{
  Tests.append(new TestServerClient(create_port, "iocp-server-client"));
}
