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
PosixPoller::Attach(Ref<Transport> baseTransport, Ref<StatusListener> listener, EventFlags eventMask)
{
  PosixTransport *transport = baseTransport->toPosixTransport();
  if (!transport)
    return eIncompatibleTransport;

  AutoMaybeLock lock(lock_);
  if (transport->attached())
    return eTransportAlreadyAttached;
  if (transport->fd() == -1)
    return eTransportClosed;

  assert(listener);
  assert(int(Event_Read) == int(kTransportReading));
  assert(int(Event_Write) == int(kTransportWriting));
  assert(int(Event_Sticky) == int(kTransportSticky));

  return attach_locked(transport, listener, TransportFlags(eventMask));
}

void
PosixPoller::Detach(Ref<Transport> baseTransport)
{
  PosixTransport *transport = baseTransport->toPosixTransport();
  if (!transport)
    return;

  // We must acquire the lock before checking the poller, since we could be
  // detaching on another thread.
  AutoMaybeLock lock(lock_);
  if (transport->poller() != this)
    return;

  detach_locked(transport);
}

PassRef<IOError>
PosixPoller::ChangeStickyEvents(Ref<Transport> baseTransport, EventFlags eventMask)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport)
    return eIncompatibleTransport;

  AutoMaybeLock lock(lock_);
  if (transport->poller() != this)
    return eIncompatibleTransport;
  if (transport->fd() == -1)
    return eTransportClosed;
  if (!(transport->flags() & kTransportSticky))
    return eIncompatibleTransport;
  if (!(eventMask & Event_Sticky))
    return eIncompatibleTransport;

  TransportFlags flags = kTransportNoFlags;
  if (eventMask & Event_Read)
    flags |= kTransportReading;
  if (eventMask & Event_Write)
    flags |= kTransportWriting;

  // Check if the events are the same.
  if ((transport->flags() & kTransportEventMask) == flags)
    return nullptr;

  return change_events_unlocked(transport, flags|kTransportSticky);
}

void
PosixPoller::detach_unlocked(PosixTransport *transport)
{
  AutoMaybeLock lock(lock_);

  if (transport->poller() != this)
    return;

  detach_locked(transport);
}

PassRef<IOError>
PosixPoller::change_events_unlocked(PosixTransport *transport, TransportFlags flags)
{
  AutoMaybeLock lock(lock_);

  if (transport->poller() != this)
    return eIncompatibleTransport;

  return change_events_locked(transport, flags);
}

PassRef<IOError>
PosixPoller::add_events_unlocked(PosixTransport *transport, TransportFlags flags)
{
  AutoMaybeLock lock(lock_);

  if (transport->poller() != this)
    return eIncompatibleTransport;

  return change_events_locked(transport, transport->flags() | flags);
}

// This is called within the poll lock.
void
PosixPoller::reportHup_locked(Ref<PosixTransport> transport)
{
  // Get a local copy of the listener before we wipe it out.
  Ref<StatusListener> listener = transport->listener();
  detach_locked(transport);

  AutoMaybeUnlock unlock(lock_);
  listener->OnHangup(transport);
}

void
PosixPoller::reportError_locked(Ref<PosixTransport> transport)
{
  reportError_locked(transport, eUnknownHangup);
}

// This is called within the poll lock.
void
PosixPoller::reportError_locked(Ref<PosixTransport> transport, Ref<IOError> error)
{
  // Get a local copy of the listener before we wipe it out.
  Ref<StatusListener> listener = transport->listener();
  detach_locked(transport);

  AutoMaybeUnlock unlock(lock_);
  listener->OnError(transport, error);
}

void
PosixPoller::EnableThreadSafety()
{
  lock_ = new Mutex();
  poll_lock_ = new Mutex();
}
