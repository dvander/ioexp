// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
#ifndef _include_amio_windows_base_poller_h_
#define _include_amio_windows_base_poller_h_

#include <amio-windows.h>
#include <am-atomics.h>

namespace amio {

using namespace ke;

class WinBasePoller : public Poller
{
 public:
  WinBasePoller() : pending_events_(0)
  { }
  void addPendingEvent() {
    Ops::Increment(&pending_events_);
  }
  void removePendingEvent() {
    Ops::Decrement(&pending_events_);
  }

 protected:
  typedef AtomicOps<sizeof(uintptr_t)> Ops;

  Ops::Type pending_events_;
};

class WinSocket;

class WinBaseSocketPoller : public SocketPoller
{
 public:
  // Notifies the pump that the socket would block reading.
  virtual void onReadWouldBlock(WinSocket *transport) = 0;

  // Notifies the pump that the socket would block writing.
  virtual PassRef<IOError> onWriteWouldBlock(WinSocket *transport) = 0;

  // Notifies the pump that a socket should be removed from the event list.
  virtual void unhook(WinSocket *transport) = 0;
};

} // namespace amio

#endif // _include_amio_windows_base_poller_h_
