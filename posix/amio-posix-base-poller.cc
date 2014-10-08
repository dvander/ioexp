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
#include "include/amio-posix.h"
#include "posix/amio-posix-transport.h"
#include "posix/amio-posix-base-poller.h"
#include "posix/amio-posix-errors.h"

using namespace ke;
using namespace amio;

PassRef<IOError>
PosixPoller::toPosixTransport(PosixTransport **outp, Transport *baseTransport)
{
  if ((*outp = baseTransport->toPosixTransport()) == nullptr)
    return eIncompatibleTransport;
  if ((*outp)->pump())
    return eTransportAlreadyAttached;
  if ((*outp)->fd() == -1)
    return eTransportClosed;
  return nullptr;
}

void
PosixPoller::reportHup(Ref<PosixTransport> transport)
{
  // Get a local copy of the listener before we wipe it out.
  Ref<StatusListener> listener = transport->listener();
  unhook(transport);
  listener->OnHangup(transport);
}

void
PosixPoller::reportError(Ref<PosixTransport> transport)
{
  // Get a local copy of the listener before we wipe it out.
  Ref<StatusListener> listener = transport->listener();
  unhook(transport);
  listener->OnError(transport, eUnknownHangup);
}

PassRef<PosixTransport>
PosixPoller::validateEventChange(Ref<Transport> baseTransport, EventFlags eventMask)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport || transport->pump() != this || transport->fd() == -1)
    return nullptr;
  if (!(eventMask & Event_Sticky))
    return nullptr;
  return transport;
}
