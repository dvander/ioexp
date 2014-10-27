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
#include "../testing.h"

using namespace ke;
using namespace amio;

class TestEventQueues
 : public Test,
   public ke::Refcounted<TestEventQueues>,
#if defined(KE_POSIX)
   public StatusListener
#elif defined(KE_WINDOWS)
   public IOListener
#endif
{
 public:
  TestEventQueues()
   : Test("event-queues"),
     nevents_(0)
  {}

  KE_IMPL_REFCOUNTING(TestEventQueues);

  bool test_basic() {
    Ref<Poller> poller;
    if (!check_error(PollerFactory::Create(&poller), "create poller"))
      return false;

    Ref<EventQueue> evq(EventQueue::Create(poller));

    Vector<Ref<Transport>> readers;
    Vector<Ref<Transport>> writers;
    for (size_t i = 0; i < 10; i++) {
      Ref<Transport> reader, writer;
      if (!check_error(TransportFactory::CreatePipe(&reader, &writer), "create pipe"))
        return false;
      readers.append(reader);
      writers.append(writer);
#if defined(KE_POSIX)
      if (!check_error(evq->Attach(writer, this, Events::Write, EventMode::Level), "attach writer"))
        return false;
#else
#endif
    }

    nevents_ = 0;
    if (!check_error(poller->Poll(), "poll"))
      return false;
    if (!check(evq->DispatchEvents(), "dispatch should process events"))
      return false;
    if (!check(nevents_ == 10, "should get 10 reads, got %d", nevents_))
      return false;

    // Detach writer #0.
    nevents_ = 0;
#if defined(KE_POSIX)
    poller->Detach(writers[0]);
#else
    writers[0]->Close();
#endif
    if (!check_error(poller->Poll(), "poll"))
      return false;

    // Detach writer #1, while enqueued.
#if defined(KE_POSIX)
    poller->Detach(writers[1]);
#else
    writers[1]->Close();
#endif

    // Close writer #2, then poll again.
    writers[2]->Close();
    if (!check_error(poller->Poll(), "poll"))
      return false;

    // Writer #1 should have been removed from the task list.
    // Writer #2 should have triggered a read, which should be ignored.
    // Writer #3 should have triggered a hangup, but still triggered a read.
    if (!check(evq->DispatchEvents(), "dispatch should process events"))
      return false;
    if (!check(nevents_ == 7, "should get 7 reads, got %d", nevents_))
      return false;

    // Close reader #3. This should have no effect.
    readers[3]->Close();

    // There should only be 7 listeners left.
    nevents_ = 0;
    if (!check_error(poller->Poll(), "poll"))
      return false;
    if (!check(evq->DispatchEvents(), "dispatch should process events"))
      return false;
    if (!check(nevents_ == 7, "should get 7 reads, got %d", nevents_))
      return false;

    return true;
  }

#if defined(KE_POSIX)
  void OnWriteReady() override {
    nevents_++;
  }
#endif

  bool Run() override {
    if (!test_basic())
      return false;
    return true;
  }

 private:
  unsigned nevents_;
};

class SetupEventQueueTests
{
 public:
  SetupEventQueueTests() {
    Tests.append(new TestEventQueues());
  }
} sSetupEventQueueTests;
