// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include <amio.h>
#include <amio-net.h>
#include <amio-eventloop.h>
#include <am-thread-utils.h>
#include "../testing.h"

using namespace ke;
using namespace amio;

class PokeThread : public IRunnable
{
 public:
  PokeThread(EventLoopForIO *me, EventLoopForIO *other)
   : me_(me),
     other_(other)
  {
    Ended = false;
  }

  void Run() override {
    me_->Loop();
    other_->PostQuit();
    Ended = true;
  }

  bool Ended;

 private:
  EventLoopForIO *me_;
  EventLoopForIO *other_;
};

class RemoteQuit : public Task
{
 public:
  RemoteQuit(EventLoopForIO *loop)
   : loop_(loop)
  {}

  void Run() override {
    loop_->PostQuit();
  }

 private:
  EventLoopForIO *loop_;
};

class TestEventLoops
 : public Test,
   public ke::Refcounted<TestEventLoops>,
#if defined(KE_POSIX)
   public StatusListener
#elif defined(KE_WINDOWS)
   public IOListener
#endif
{
 public:
  TestEventLoops()
   : Test("event-loops"),
     nevents_(0)
  {}

  KE_IMPL_REFCOUNTING(TestEventLoops);

  bool test_basic() {
    Ref<EventLoopForIO> evq, remote;
    if (!check_error(EventLoopForIO::Create(&evq, nullptr), "create local loop"))
      return false;
    if (!check_error(EventLoopForIO::Create(&remote, nullptr), "create remote loop"))
      return false;

    AutoPtr<PokeThread> poke(new PokeThread(remote, evq));
    AutoPtr<Thread> poke_thread(new Thread(poke));
    if (!check(poke_thread->Succeeded(), "start remote thread"))
      return false;

    // When we enter Loop(), we should process the task posting a quit to the
    // remote thread. This should wake up the remote thread, which in turn
    // should post a quit message back to us.
    evq->PostTask(new RemoteQuit(remote));
    evq->Loop();

    if (!check(poke->Ended, "remote thread received quit"))
      return false;

    return true;
  }

  void OnWriteReady() override {
    nevents_++;
  }

  bool Run() override {
    if (!test_basic())
      return false;
    return true;
  }

 private:
  unsigned nevents_;
};

class SetupEventLoopTests
{
 public:
  SetupEventLoopTests() {
    Tests.append(new TestEventLoops());
  }
} sSetupEventLoopTests;

