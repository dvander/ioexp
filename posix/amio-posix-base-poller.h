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

namespace amio {

class PosixPoller
  : public Poller,
    public ke::Refcounted<PosixPoller>
{
 public:
  // Notifies the pump that the socket would block reading.
  virtual ke::PassRef<IOError> onReadWouldBlock(PosixTransport *transport) = 0;

  // Notifies the pump that the socket would block writing.
  virtual ke::PassRef<IOError> onWriteWouldBlock(PosixTransport *transport) = 0;

  // Notifies the pump that a socket should be removed from the event list.
  // Note that we don't use Ref<> here, since we potentially call this from
  // the transport's destructor, we don't want to revive the object.
  virtual void unhook(PosixTransport *transport) = 0;

  PassRef<IOError> toPosixTransport(PosixTransport **outp, Transport *transport);
  PassRef<PosixTransport> validateEventChange(
    Ref<Transport> baseTransport,
    EventFlags eventMask
  );

  void AddRef() override {
    ke::Refcounted<PosixPoller>::AddRef();
  }
  void Release() override {
    ke::Refcounted<PosixPoller>::Release();
  }

  void reportHup(Ref<PosixTransport> transport);
  void reportError(Ref<PosixTransport> transport);
  void reportError(Ref<PosixTransport> transport, Ref<IOError> error);
};

} // namespace amio

#endif // _include_amio_base_pump_h_
