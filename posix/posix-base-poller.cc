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
#include "shared/shared-errors.h"
#include "posix/posix-transport.h"
#include "posix/posix-base-poller.h"
#include "posix/posix-errors.h"

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

static inline Events
TransportFlagsToEvents(TransportFlags flags)
{
  return Events(flags & (kTransportReading|kTransportWriting));
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
  if ((mode & EventMode::Proxy) == EventMode::Proxy)
    flags |= kTransportProxying;

  return attach_locked(transport, listener, flags);
}

void
PosixPoller::Detach(Ref<Transport> baseTransport)
{
  PosixTransport *transport = baseTransport->toPosixTransport();
  if (!transport)
    return;

  detach_unlocked(transport);
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
  bool proxying;
  Ref<StatusListener> proxy;
  {
    AutoMaybeLock lock(lock_);
    if (transport->poller() != this)
      return;

    proxying = transport->isProxying();
    proxy = detach_locked(transport);
  }

  if (proxying)
    proxy->OnProxyDetach();
}

PassRef<IOError>
PosixPoller::add_events_locked(PosixTransport *transport, TransportFlags flags)
{
  return change_events_locked(transport, transport->flags() | flags);
}

PassRef<IOError>
PosixPoller::rm_events_locked(PosixTransport *transport, TransportFlags flags)
{
  return change_events_locked(transport, transport->flags() & ~flags);
}

PassRef<IOError>
PosixPoller::change_events_unlocked(PosixTransport *transport, TransportFlags flags)
{
  AutoMaybeLock lock(lock_);

  if (transport->fd() == -1)
    return eTransportClosed;
  if (transport->poller() != this)
    return eIncompatibleTransport;

  return change_events_locked_helper(&lock, transport, flags);
}

PassRef<IOError>
PosixPoller::add_events_unlocked(PosixTransport *transport, TransportFlags flags)
{
  AutoMaybeLock lock(lock_);

  if (transport->fd() == -1)
    return eTransportClosed;
  if (transport->poller() != this)
    return eIncompatibleTransport;

  return change_events_locked_helper(&lock, transport, transport->flags() | flags);
}

PassRef<IOError>
PosixPoller::rm_events_unlocked(PosixTransport *transport, TransportFlags flags)
{
  AutoMaybeLock lock(lock_);

  if (transport->fd() == -1)
    return eTransportClosed;
  if (transport->poller() != this)
    return eIncompatibleTransport;

  return change_events_locked_helper(&lock, transport, transport->flags() & ~flags);
}

PassRef<IOError>
PosixPoller::change_events_locked_helper(AutoMaybeLock *mlock,
                                         PosixTransport *transport,
                                         TransportFlags flags)
{
  if (transport->flags() == flags)
    return nullptr;
  if (Ref<IOError> error = change_events_locked(transport, flags))
    return error;

  if (transport->isProxying()) {
    Ref<StatusListener> proxy = transport->listener();

    mlock->unlock();
    proxy->OnChangeEvents(TransportFlagsToEvents(flags));
  }
  return nullptr;
}

// This is called within the poll lock.
void
PosixPoller::reportHup_locked(Ref<PosixTransport> transport)
{
  // Get a local copy of the listener before we wipe it out.
  Ref<StatusListener> listener = detach_locked(transport);

  AutoMaybeUnlock unlock(lock_);
  listener->OnHangup(nullptr);
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
  Ref<StatusListener> listener = detach_locked(transport);

  AutoMaybeUnlock unlock(lock_);
  listener->OnHangup(error);
}

void
PosixPoller::detach_for_shutdown_locked(PosixTransport *transport)
{
  if (transport->isProxying()) {
    if (Ref<StatusListener> listener = transport->detach()) {
      AutoMaybeUnlock unlock(lock_);
      listener->OnProxyDetach();
    }
  } else {
    transport->detach();
  }
}

void
PosixPoller::EnableThreadSafety()
{
  lock_ = new Mutex();
  poll_lock_ = new Mutex();
}
