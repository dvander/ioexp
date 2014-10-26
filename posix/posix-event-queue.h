// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_eventqueue_h_
#define _include_amio_eventqueue_h_

#include <amio-eventloop.h>
#include <am-inlinelist.h>
#include "../shared/shared-task-queue.h"

namespace amio {

using namespace ke;

class EventQueueImpl
 : public EventQueue,
   public ke::Refcounted<EventQueueImpl>
{
 public:
  EventQueueImpl(Ref<Poller> poller);
  ~EventQueueImpl();

  KE_IMPL_REFCOUNTING(EventQueueImpl);

  PassRef<IOError> Attach(Ref<Transport> transport, Ref<StatusListener> listener,
                          Events events, EventMode mode) override;
  void Detach(Ref<Transport> transport) override;
  bool DispatchNextEvent() override;
  bool DispatchEvents(struct timeval *timelimitp = nullptr, size_t nlimit = 0) override;
  void Break() override;
  void Shutdown() override;

  PassRef<IOError> ChangeEvents(Ref<Transport> transport, Events events) override;
  PassRef<IOError> AddEvents(Ref<Transport> transport, Events events) override;
  PassRef<IOError> RemoveEvents(Ref<Transport> transport, Events events) override;

 private:
  class Delegate
   : public StatusListener,
     public Task,
     public InlineListNode<Delegate>,
     public ke::Refcounted<Delegate>
  {
    friend class EventQueueImpl;

   public:
    Delegate(EventQueueImpl *parent, Ref<Transport> transport, Ref<StatusListener> forward)
     : parent_(parent),
       transport_(transport),
       forward_(forward),
       events_(Events::None)
    {}

    KE_IMPL_REFCOUNTING(Delegate);

    void Run() override;
    void DeleteMe() override {
      Release();
    }

    void OnReadReady() override;
    void OnWriteReady() override;
    void OnHangup(Ref<IOError> error) override;
    void OnProxyDetach() override;
    void OnChangeProxy(Ref<StatusListener> new_listener) override;
    void OnChangeEvents(Events events) override;

   private:
    void MaybeEnqueue();

   private:
    EventQueueImpl *parent_; // Not Ref<>, this would form cycles.
    Ref<Transport> transport_;
    Ref<StatusListener> forward_;
    Events events_;
    Ref<IOError> error_;
  };

 private:
  void remove_delegate(Delegate *delegate);

 private:
  Ref<Poller> poller_;
  AutoPtr<TaskQueueImpl> tasks_;
  InlineList<Delegate> delegates_;
};

} // namespace amio

#endif // _include_amio_eventqueue_h_
