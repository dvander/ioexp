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

#include <amio.h>
#include <am-atomics.h>
#include <am-thread-utils.h>

namespace amio {

using namespace ke;

class WinBasePoller
 : public Poller,
   public ke::RefcountedThreadsafe<WinBasePoller>
{
 public:
  WinBasePoller();

  KE_IMPL_REFCOUNTING_TS(WinBasePoller);

  PassRef<IOError> Attach(Ref<Transport> transport, Ref<IOListener> listener) override;
  PassRef<IOError> Post(Ref<IOContext> context, Ref<IOListener> listener) override;
  bool EnableImmediateDelivery() override;
  bool RequireImmediateDelivery() override;
  void EnableThreadSafety() override;

  WinBasePoller *toWinBasePoller() override {
    return this;
  }
  Mutex *lock() {
    return lock_;
  }

  virtual size_t NumConcurrentThreads() = 0;
  
  virtual PassRef<IOError> attach_unlocked(WinTransport *transport, IOListener *listener) = 0;
  virtual PassRef<IOError> post_unlocked(WinContext *context, IOListener *listener) = 0;
  virtual bool enable_immediate_delivery_locked() = 0;

 public:
  template <typename T>
  void link(WinContext *context, const T &object, RequestType type) {
    if (object)
      object->AddRef();

    this->link_impl(context, type);
  }
  template <typename T>
  void unlink(WinContext *context, const T &object) {
    this->unlink_impl(context);

    if (object)
      object->Release();
  }

 private:
  void link_impl(WinContext *context, RequestType type);
  void unlink_impl(WinContext *context);

 protected:
  void addPendingEvent() {
    Ops::Increment(&pending_events_);
  }
  void removePendingEvent() {
    Ops::Decrement(&pending_events_);
  }

 protected:
  typedef AtomicOps<sizeof(uintptr_t)> Ops;

  Ops::Type pending_events_;
  AutoPtr<Mutex> lock_;

  bool immediate_delivery_;
  bool immediate_delivery_required_;
};

} // namespace amio

#endif // _include_amio_windows_base_poller_h_
