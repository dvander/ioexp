// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_base_pump_h_
#define _include_amio_base_pump_h_

#include "include/amio.h"
#include "include/amio-posix.h"
#include "posix/amio-posix-transport.h"
#include <am-thread-utils.h>

namespace amio {

using namespace ke;

// Baseline for posix transports. Note that some internal functions take in
// raw pointers. In these cases, we expect that the caller is hoding the
// pointer alive in a Ref.
class PosixPoller
  : public Poller,
    public RefcountedThreadsafe<PosixPoller>
{
 public:
  void EnableThreadSafety() override;

  void AddRef() override {
    RefcountedThreadsafe<PosixPoller>::AddRef();
  }
  void Release() override {
    RefcountedThreadsafe<PosixPoller>::Release();
  }
  size_t MaximumConcurrency() override {
    return 1;
  }

  // Helper functions. These perform validation and route on to inner
  // functions.
  PassRef<IOError> Attach(
    Ref<Transport> baseTransport,
    Ref<StatusListener> listener,
    Events events,
    EventMode mode
  ) override;

  void Detach(Ref<Transport> baseTransport) override;

  PassRef<IOError> ChangeEvents(Ref<Transport> transport, Events events) override;
  PassRef<IOError> AddEvents(Ref<Transport> transport, Events events) override;
  PassRef<IOError> RemoveEvents(Ref<Transport> transport, Events events) override;

  // These are called after validation.
  virtual PassRef<IOError> attach_locked(
    PosixTransport *transport,
    StatusListener *listener,
    TransportFlags flags) = 0;
  virtual void detach_locked(PosixTransport *transport) = 0;
  virtual PassRef<IOError> change_events_locked(PosixTransport *transport, TransportFlags flags) = 0;

  // Operations that have not yet acquired the poller lock.
  void detach_unlocked(PosixTransport *transport);
  PassRef<IOError> change_events_unlocked(PosixTransport *transport, TransportFlags flags);
  PassRef<IOError> add_events_unlocked(PosixTransport *transport, TransportFlags flags);
  PassRef<IOError> rm_events_unlocked(PosixTransport *transport, TransportFlags flags);

  // Helpers.
  PassRef<IOError> add_events_locked(PosixTransport *transport, TransportFlags flags);
  PassRef<IOError> rm_events_locked(PosixTransport *transport, TransportFlags flags);
  void reportHup_locked(Ref<PosixTransport> transport);
  void reportError_locked(Ref<PosixTransport> transport);
  void reportError_locked(Ref<PosixTransport> transport, Ref<IOError> error);

 protected:
  AutoPtr<Mutex> lock_;
  AutoPtr<Mutex> poll_lock_;
};

} // namespace amio

#endif // _include_amio_base_pump_h_
