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
#include "bsd/amio-bsd-kqueue.h"

using namespace amio;
using namespace ke;

Ref<IOError>
PollerFactory::CreateKqueueImpl(Poller **outp, size_t maxEventsPerPoll)
{
  AutoPtr<KqueueImpl> poller(new KqueueImpl(maxEventsPerPoll));
  Ref<IOError> error = poller->Initialize();
  if (error)
    return error;
  *outp = poller.take();
  return nullptr;
}

Ref<IOError>
PollerFactory::CreatePoller(Poller **outp)
{
  return CreateKqueueImpl(outp, kDefaultMaxEventsPerPoll);
}