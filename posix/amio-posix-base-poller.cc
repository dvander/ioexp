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
#include "shared/amio-errors.h"
#include "posix/amio-posix-transport.h"
#include "posix/amio-posix-base-poller.h"
#include "posix/amio-posix-errors.h"

using namespace ke;
using namespace amio;

static inline TransportFlags
EventsToFlags(Events events)
{
  assert(int(Events::Read) == int(kTransportReading));
  assert(int(Events::Write) == int(kTransportWriting));
  assert(int(EventMode::Level) == int(kTransportLT));
  assert(int(EventMode::Edge) == int(kTransportET));

  return TransportFlags(events);
}

PassRef<IOError>
PosixPoller::Attach(Ref<Transport> baseTransport, Ref<StatusListener> listener, Events events, EventMode mode)
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

  if (mode == EventMode::Edge && !SupportsEdgeTriggering())
    return eEdgeTriggeringUnsupported;
  if (mode == EventMode::ETS)
    mode = EventMode::Edge;

  TransportFlags flags = EventsToFlags(events) | TransportFlags(mode);

  return attach_locked(transport, listener, flags);
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
PosixPoller::ChangeEvents(Ref<Transport> baseTransport, Events events)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport)
    return eIncompatibleTransport;

  return change_events_unlocked(transport, EventsToFlags(events));
}

PassRef<IOError>
PosixPoller::AddEvents(Ref<Transport> baseTransport, Events events)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport)
    return eIncompatibleTransport;

  return add_events_unlocked(transport, EventsToFlags(events));
}

PassRef<IOError>
PosixPoller::RemoveEvents(Ref<Transport> baseTransport, Events events)
{
  Ref<PosixTransport> transport(baseTransport->toPosixTransport());
  if (!transport)
    return eIncompatibleTransport;

  return rm_events_unlocked(transport, EventsToFlags(events));
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

  if (transport->fd() == -1)
    return eTransportClosed;
  if (transport->poller() != this)
    return eIncompatibleTransport;

  if ((transport->flags() & kTransportEventMask) == flags)
    return nullptr;
  return change_events_locked(transport, flags);
}

PassRef<IOError>
PosixPoller::add_events_unlocked(PosixTransport *transport, TransportFlags flags)
{
  AutoMaybeLock lock(lock_);

  if (transport->fd() == -1)
    return eTransportClosed;
  if (transport->poller() != this)
    return eIncompatibleTransport;

  if ((transport->flags() | flags) == flags)
    return nullptr;
  return change_events_locked(transport, transport->flags() | flags);
}

PassRef<IOError>
PosixPoller::rm_events_unlocked(PosixTransport *transport, TransportFlags flags)
{
  AutoMaybeLock lock(lock_);

  if (transport->fd() == -1)
    return eTransportClosed;
  if (transport->poller() != this)
    return eIncompatibleTransport;

  if ((transport->flags() & ~flags) == flags)
    return nullptr;
  return change_events_locked(transport, transport->flags() & ~flags);
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
