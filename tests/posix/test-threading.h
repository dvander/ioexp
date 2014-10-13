// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_test_posix_threading_h_
#define _include_amio_test_posix_threading_h_

#include <amio.h>
#include "../testing.h"

namespace amio {

class TestThreading
 : public virtual StatusListener,
   public virtual Test
{
 public:
  TestThreading(CreatePoller_t ctor, const char *name);

  bool Run() override;
  void AddRef() override {
    Test::AddRef();
  }
  void Release() override {
    Test::Release();
  }

 private:
  bool finished();
 
 private:
  CreatePoller_t constructor_;
  Ref<Poller> poller_;
  Ref<Transport> reader_;
  Ref<Transport> writer_;
};

}

#endif // _include_amio_test_posix_threading_h_
