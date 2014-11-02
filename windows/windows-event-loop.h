// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_windows_event_loop_h_
#define _include_amio_windows_event_loop_h_

#include <amio.h>
#include <amio-eventloop.h>
#include "../shared/shared-task-queue.h"

namespace amio {

using namespace ke;

class WindowsEventLoopForIO
 : public EventLoopForIO,
   public TaskQueue::Delegate,
   public RefcountedThreadsafe<WindowsEventLoopForIO>
{
 public:
  WindowsEventLoopForIO(Ref<Poller> poller);

  KE_IMPL_REFCOUNTING_TS(WindowsEventLoopForIO);

  PassRef<Poller> GetPoller() override;

  PassRef<IOError> Attach(Ref<Transport> transport, Ref<IOListener> listener) override;

  void PostTask(Task *task) override;
  void PostQuit() override;
  bool ShouldQuit() override;
  void Loop() override;
  void Shutdown() override;

  void NotifyTask() override;
  void NotifyQuit() override;

 private:
  class Wakeup
   : public IOListener,
     public RefcountedThreadsafe<Wakeup>
  {
   public:
    Wakeup();
    KE_IMPL_REFCOUNTING_TS(Wakeup);

    void OnCompleted(IOResult &r);
    void Signal(Ref<Poller> poller);

   private:
    Ref<IOContext> context_;
  };

 private:
  Ref<Poller> poller_;
  TaskQueueImpl tasks_;
  Ref<Wakeup> wakeup_;
  bool received_wakeup_;
};

} // namespace amio

#endif // _include_amio_windows_event_loop_h_