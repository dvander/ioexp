// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
#ifndef _include_amio_windows_select_h_
#define _include_amio_windows_select_h_

#include "amio-windows-base-poller.h"
#include <am-vector.h>

namespace amio {

using namespace ke;

class WinSocket;

class SelectImpl : public WinBaseSocketPoller
{
 public:
  SelectImpl();
  ~SelectImpl();

  PassRef<IOError> Poll(int timeoutMs = kNoTimeout) override;
  PassRef<IOError> Attach(Ref<Socket> socket, Ref<SocketListener> listener) override;
  void Detach(Ref<Socket> socket) override;

  void onReadWouldBlock(WinSocket *socket) override;
  PassRef<IOError> onWriteWouldBlock(WinSocket *socket) override;
  void unhook(WinSocket *socket) override;

 private:
  struct PollData {
    Ref<WinSocket> socket;
    uintptr_t modified;
    bool watching_reads;
    bool watching_writes;
  };

  fd_set read_fds_;
  fd_set write_fds_;
  ke::Vector<PollData> fds_;
  ke::Vector<size_t> free_slots_;
  uintptr_t generation_;
};

} // namespace amio

#endif // _include_amio_windows_select_h_