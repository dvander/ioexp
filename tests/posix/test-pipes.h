// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_test_posix_pipes_h_
#define _include_amio_test_posix_pipes_h_

#include <amio.h>
#include "../testing.h"

namespace amio {

class TestPipes
 : public virtual StatusListener,
   public virtual Test
{
 public:
  TestPipes(CreatePoller_t ctor, const char *name);

  bool Run() override;
  void AddRef() override {
    Test::AddRef();
  }
  void Release() override {
    Test::Release();
  }

  void OnReadReady(Ref<Transport> transport) override {
    got_read_ = true;
    assert(transport == reader_);
  }
  void OnWriteReady(Ref<Transport> transport) override {
    got_write_ = true;
    assert(transport == writer_);
  }
  void OnHangup(Ref<Transport> transport) override {
    got_hangup_ = true;
    got_error_ = nullptr;
  }
  void OnError(Ref<Transport> transport, Ref<IOError> error) override {
    got_hangup_ = true;
    got_error_ = error;
  }

 private:
  bool setup(EventFlags extra = Events_None);
  void reset();

  bool test_read_write();
  bool test_poll_write_close();
  bool test_poll_read_close();
  bool test_sticky();
  bool test_edge_triggering();

  bool wait_for_read();
  bool wait_for_write();

  bool write(const char *msg, size_t len);

 private:
  CreatePoller_t constructor_;
  AutoPtr<Poller> poller_;
  Ref<Transport> reader_;
  Ref<Transport> writer_;
  bool got_read_;
  bool got_write_;
  Ref<IOError> got_error_;
  bool got_hangup_;
};

}

#endif // _include_amio_test_posix_pipes_h_
