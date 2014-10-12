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

static const size_t kDefaultMaxEventsPerPortPoll;

class CompletionPort : public WinBasePoller
{
 public:
  CompletionPort();
  ~CompletionPort();
  PassRef<IOError> Initialize(size_t numConcurrentThreads);

  PassRef<IOError> Poll(int timeoutMs) override;
  PassRef<IOError> Attach(Ref<Transport> transport, Ref<IOListener> listener) override;
  bool EnableImmediateDelivery() override;
  bool RequireImmediateDelivery() override;
  void WaitAndDiscardPendingEvents() override;

  size_t NumConcurrentThreads() override {
    return concurrent_threads_;
  }

 private:
  PassRef<IOError> InternalPoll(int timeoutMs, size_t *nevents);

 private:
  HANDLE port_;
  size_t concurrent_threads_;
  bool immediate_delivery_;
  bool immediate_delivery_required_;
};

} // namespace amio

#endif // _include_amio_windows_iocp_h_
