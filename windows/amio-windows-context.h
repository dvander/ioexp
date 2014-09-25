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

class WinContext : public IOContext
{
 public:
  WinContext(uintptr_t data = 0);

  uintptr_t UserData() const override {
    return data_;
  }
  uintptr_t SetUserData(uintptr_t data) override {
    uintptr_t old = data_;
    data_ = data;
    return old;
  }
  OVERLAPPED *ov() {
    return &ov_;
  }
  WinContext *toWinContext() {
    return this;
  }

  static WinContext *fromOverlapped(OVERLAPPED *op) {
    return reinterpret_cast<WinContext *>(
      reinterpret_cast<uint8_t *>(op) - offsetof(WinContext, ov_)
    );
  }

  bool associated() const {
    return associated_;
  }
  void unassociate() {
    associated_ = false;
  }
  void associate() {
    associated_ = true;
  }

 private:
  OVERLAPPED ov_;
  uintptr_t data_;
  bool associated_;
};

} // namespace amio

#endif // _include_amio_windows_context_h_
