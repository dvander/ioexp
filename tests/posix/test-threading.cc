// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson, AlliedModders LLC
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include <amio.h>
#include <string.h>
#include "test-threading.h"
#include <am-thread-utils.h>

using namespace ke;
using namespace amio;

TestThreading::TestThreading(CreatePoller_t ctor, const char *name)
 : Test(name),
   constructor_(ctor)
{
}

class TestThread
 : public IRunnable,
   public StatusListener,
   public ke::Refcounted<TestThread>
{
 public:
  TestThread(Ref<Poller> poller)
   : Exited(false),
     Errored(false),
     poller_(poller),
     got_write_(false),
     got_read_(false)
  {
    cond_ = new ConditionVariable();
  }

  void OnWriteReady() override {
    AutoLock lock(cond_);
    got_write_ = true;
    cond_->Notify();
  }

  void OnReadReady() override {
    AutoLock lock(cond_);
    got_read_ = true;
    cond_->Notify();
  }

  void AddRef() override {
    ke::Refcounted<TestThread>::AddRef();
  }
  void Release() override {
    ke::Refcounted<TestThread>::Release();
  }
  void Run() override {
    if (!check_error(TransportFactory::CreatePipe(&reader, &writer), "create pipes"))
      return;
    if (!check_error(poller_->Attach(reader, this, Events::None, EventMode::ETS), "attach read pipe"))
      return;
    if (!check_error(poller_->Attach(writer, this, Events::Write, EventMode::ETS), "attach write pipe"))
      return;

    IOResult r;
    char buf[5] = {0};
    if (!check(reader->Read(&r, &buf, sizeof(buf)), "read from pipe")) {
      check_error(r.error, "read from pipe");
      return;
    }
    if (!check(!r.completed, "read should not have completed"))
      return;

    // Wait until we write at least one byte.
    while (true) {
      AutoLock lock(cond_);
      while (!got_write_)
        cond_->Wait();

      if (!check(writer->Write(&r, &buf, 1), "write to pipe")) {
        check_error(r.error, "write to pipe");
        return;
      }
      if (r.completed)
        break;

      got_write_ = false;
    }

    // Wait until we get a read.
    while (!got_read_) {
      AutoLock lock(cond_);
      while (!got_read_)
        cond_->Wait();
    }

    Exited = true;
  }

  bool check(bool cond, const char *fmt) {
    if (!::check(cond, "%s", fmt)) {
      Errored = true;
      return false;
    }
    return true;
  }
  bool check_error(Ref<IOError> error, const char *fmt) {
    if (!::check_error(error, "%s", fmt)) {
      Errored = true;
      return false;
    }
    return true;
  }

  bool Exited;
  bool Errored;

  Ref<Transport> reader;
  Ref<Transport> writer;

 private:
  Ref<Poller> poller_;
  AutoPtr<ConditionVariable> cond_;
  bool got_write_;
  bool got_read_;
};

static Ref<TestThread> sCtrl;

bool
TestThreading::finished()
{
  if (sCtrl->Errored || sCtrl->Exited)
    return true;
  return false;
}

bool
TestThreading::Run()
{
  Ref<IOError> error = constructor_(&poller_);
  if (!check_error(error, "create poller"))
    return false;
  poller_->EnableThreadSafety();

  Ref<TestThread> ctrl = new TestThread(poller_);
  AutoPtr<Thread> thread(new Thread(ctrl, "test thread"));
  if (!check(thread->Succeeded(), "thread started"))
    return false;

  sCtrl = ctrl;
  while (!finished()) {
    if (Ref<IOError> error = poller_->Poll(kSafeTimeout)) {
      check_error(error, "Poll()");
      return false;
    }
  }
  sCtrl = nullptr;

  if (ctrl->reader)
    ctrl->reader->Close();
  if (ctrl->writer)
    ctrl->writer->Close();

  if (ctrl->Errored)
    return false;

  poller_ = nullptr;
  return true;
}
