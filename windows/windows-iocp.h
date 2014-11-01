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

#include <amio.h>
#include "windows-base-poller.h"
#include "../shared/shared-pollbuf.h"

namespace amio {

using namespace ke;

class CompletionPort : public WinBasePoller
{
 public:
  CompletionPort();
  ~CompletionPort();
  PassRef<IOError> Initialize(size_t numConcurrentThreads, size_t nMaxEventsPerPoll);

  PassRef<IOError> Poll(int timeoutMs) override;
  PassRef<IOError> PollOne(int timeoutMs) override;
  void WaitAndDiscardPendingEvents() override;

  virtual PassRef<IOError> attach_unlocked(WinTransport *transport, IOListener *listener) override;
  virtual PassRef<IOError> post_unlocked(WinContext *context, IOListener *listener) override;
  virtual bool enable_immediate_delivery_locked() override;

  size_t NumConcurrentThreads() override {
    return concurrent_threads_;
  }

 private:
  PassRef<IOError> InternalPollOne(int timeoutMs);
  bool Dispatch(WinContext *context, OVERLAPPED_ENTRY &entry, DWORD error);

 private:
  HANDLE port_;
  size_t concurrent_threads_;
  MultiPollBuffer<OVERLAPPED_ENTRY> buffers_;
};

} // namespace amio

#endif // _include_amio_windows_iocp_h_
