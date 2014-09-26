// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
#ifndef _include_amio_windows_iocp_h_
#define _include_amio_windows_iocp_h_

#include <amio-windows.h>
#include "amio-windows-base-poller.h"

namespace amio {

using namespace ke;

class CompletionPort : public WinBasePoller
{
 public:
  Ref<IOError> Initialize(size_t numConcurrentThreads);

  Ref<IOError> Poll(int timeoutMs) override;
  Ref<IOError> Attach(Ref<Transport> transport, Ref<IOListener> listener) override;
  void ForceShutdown() override;
  bool EnableImmediateDelivery() override;

 private:
  HANDLE handle_;
};

} // namespace amio

#endif // _include_amio_windows_iocp_h_
