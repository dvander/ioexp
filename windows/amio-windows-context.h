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

#include <amio-windows.h>

namespace amio {

using namespace ke;

class WinTransport;
class WinBasePoller;

class WinContext : public IOContext
{
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
  void Cancel() override;

  OVERLAPPED *ov() {
    return &ov_;
  }
  WinContext *toWinContext() override {
    return this;
  }

  OVERLAPPED *LockForOverlappedIO(Ref<Transport> transport, RequestType request) override;
  void UnlockForFailedOverlappedIO() override;

  static WinContext *fromOverlapped(OVERLAPPED *op) {
    return reinterpret_cast<WinContext *>(
      reinterpret_cast<uint8_t *>(op) - offsetof(WinContext, ov_)
    );
  }

  RequestType state() const {
    return request_;
  }
  PassRef<WinTransport> transport() {
    return transport_;
  }

  // When attaching for asynchronous IO, we add an extra ref in case the caller
  // loses all refs to the context.
  void attach(RequestType state, PassRef<WinTransport> transport);
  void detach();

 private:
  OVERLAPPED ov_;
  uintptr_t value_;
  RequestType request_;
  Ref<WinTransport> transport_;
  Ref<IUserData> data_;

  // Unfortunately we need to hold this in case the transport loses its poller.
  Ref<WinBasePoller> poller_;
};

} // namespace amio

#endif // _include_amio_windows_context_h_
