// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
#ifndef _include_amio_windows_context_h_
#define _include_amio_windows_context_h_

#include <amio.h>

namespace amio {

using namespace ke;

class WinTransport;
class WinBasePoller;

class WinContext : public IOContext
{
  friend class WinBasePoller;

 public:
  WinContext(uintptr_t value = 0);
  WinContext(Ref<IUserData> data, uintptr_t value = 0);

  uintptr_t UserValue() const override {
    return value_;
  }
  uintptr_t SetUserValue(uintptr_t data) override {
    uintptr_t old = value_;
    value_ = data;
    return old;
  }
  PassRef<IUserData> UserData() const override {
    return data_;
  }
  void SetUserData(Ref<IUserData> data) override {
    data_ = data;
  }

  OVERLAPPED *ov() {
    return &ov_;
  }
  WinContext *toWinContext() override {
    return this;
  }

  static WinContext *fromOverlapped(OVERLAPPED *op) {
    return reinterpret_cast<WinContext *>(
      reinterpret_cast<uint8_t *>(op) - offsetof(WinContext, ov_)
    );
  }

  RequestType state() const {
    return request_;
  }

  // Set the state to cancelled; returns true if the state could be changed.
  // This should be called within a Poller lock.
  bool cancel_locked();

 private:
  // When attaching for asynchronous IO, we add an extra ref in case the caller
  // loses all refs to the context.
  void attach(RequestType type);
  void detach();

 private:
  OVERLAPPED ov_;
  uintptr_t value_;
  Ref<IUserData> data_;
  RequestType request_;

  // We allow re-using contexts for Message requests (which are only created
  // through Poller::Post(). To make sure we don't clear the request type too
  // early, we use a counter.
  uintptr_t count_;
};

} // namespace amio

#endif // _include_amio_windows_context_h_
