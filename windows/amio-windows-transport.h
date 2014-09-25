// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
#ifndef _include_amio_windows_transport_h_
#define _include_amio_windows_transport_h_

#include <amio-windows.h>

namespace amio {

using namespace ke;

class WinTransport : public Transport
{
 public:
  WinTransport(HANDLE handle, TransportFlags flags);
  ~WinTransport();

  // Initiates a read operation on the supplied buffer. The buffer must be held
  // alive until the listener is notified that the event has completed. If the
  // Read() operation immediately returns an error, then no IOEvent will be
  // posted.
  virtual bool Read(IOResult *r, Ref<IOContext> context, void *buffer, size_t length) override;

  // Initiates a write operation on the supplied buffer. The buffer must be held
  // alive until the listener is notified that the event has completed. If the
  // Write() operation immediately returns an error, then no IOEvent will be
  // posted.
  virtual bool Write(IOResult *r, Ref<IOContext> context, const void *buffer, size_t length) override;

  // Helper version of Read() that automatically allocates a new context.
  //
  // An optional data value may be communicated through the event.
  IOResult Read(void *buffer, size_t length, uintptr_t data = 0);

  // Helper version of Write() that automatically allocates a new context.
  //
  // An optional data value may be communicated through the event.
  IOResult Write(const void *buffer, size_t length, uintptr_t data = 0);

  virtual void Close() override;
  WinTransport *toWinTransport() {
    return static_cast<WinTransport *>(this);
  }
  virtual bool Closed() override {
    return handle_ == INVALID_HANDLE_VALUE;
  }
  virtual HANDLE Handle() override {
    return handle_;
  }

  PassRef<IOListener> listener() const {
    return listener_;
  }

 protected:
  HANDLE handle_;
  TransportFlags flags_;
  Ref<IOListener> listener_;
};

} // namespace amio

#endif // _include_amio_windows_transport_h_