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
#include "include/amio-posix-transport.h"

namespace amio {

class PosixPump : public MessagePump
{
 public:
  // Notifies the pump that the socket would block reading.
  virtual void onReadWouldBlock(PosixTransport *transport) = 0;

  // Notifies the pump that the socket would block writing.
  virtual ke::PassRef<IOError> onWriteWouldBlock(PosixTransport *transport) = 0;

  // Notifies the pump that a socket should be removed from the event list.
  virtual void unhook(ke::Ref<PosixTransport> transport) = 0;
};

} // namespace amio

#endif // _include_amio_base_pump_h_
