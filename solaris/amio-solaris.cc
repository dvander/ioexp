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
#include "solaris/amio-solaris.h"
#include "solaris/amio-solaris-devpoll.h"
#include <string.h>
#include <stdlib.h>
#include <sys/utsname.h>

using namespace ke;
using namespace amio;

PassRef<IOError>
PollerFactory::CreateDevPollImpl(Poller **outp, size_t maxEventsPerPoll)
{
  AutoPtr<DevPollImpl> poller(new DevPollImpl());
  Ref<IOError> error = poller->Initialize(maxEventsPerPoll);
  if (error)
    return error;
  *outp = poller.take();
  return nullptr;
}

PassRef<IOError>
PollerFactory::CreatePoller(Poller **outp)
{
  return PollerFactory::CreateSelectImpl(outp);
}
