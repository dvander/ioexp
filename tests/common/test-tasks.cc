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

static AutoPtr<Mutex> sTaskMutex;
static AutoPtr<ConditionVariable> sTaskVar;
static size_t sTasksRan = 0;

class BasicTask : public Task
{
 public:
  void Run() override {
    AutoLock lock(sTaskMutex);
    sTasksRan++;
  }
};

class DelayedQuit : public Task
{
 public:
  DelayedQuit(TaskQueue *queue)
   : queue_(queue)
  {}
  void Run() override {
    queue_->PostQuit();
  }
 private:
  TaskQueue *queue_;
};

class ThreadRunner : public IRunnable
{
 public:
  ThreadRunner(TaskQueue *queue)
   : queue_(queue)
  {}
  void Run() override {
    for (size_t i = 0; i < 2000; i++)
      queue_->PostTask(new BasicTask());
    queue_->PostTask(new DelayedQuit(queue_));
  }
 private:
  TaskQueue *queue_;
};

class TestTasks
 : public Test,
   public TaskQueue::Delegate,
   public ke::Refcounted<TestTasks>
{
 public:
  TestTasks()
   : Test("task-queues")
  {
    sTaskMutex = new Mutex();
    gotQuit = false;
    hasTasks = false;
    notifications = 0;
  }

  KE_IMPL_REFCOUNTING(TestTasks);

  void NotifyTask() override {
    notifications++;
    if (sTaskVar) {
      AutoLock lock(sTaskVar);
      hasTasks = true;
      sTaskVar->Notify();
    }
  }
  void NotifyQuit() override {
    gotQuit = true;
    if (sTaskVar) {
      AutoLock lock(sTaskVar);
      sTaskVar->Notify();
    }
  }

  bool test_basic() {
    sTasksRan = 0;

    AutoPtr<TaskQueue> queue(TaskQueue::Create(this));
    if (!check(!queue->ProcessNextTask(), "should have no tasks"))
      return false;
    if (!check(!queue->ProcessTasks(nullptr, 0), "should have no tasks"))
      return false;

    notifications = 0;
    queue->PostTask(new BasicTask());
    queue->PostTask(new BasicTask());
    queue->PostTask(new BasicTask());
    if (!check(notifications == 3, "should have gotten 3 notifications"))
      return false;
    if (!check(queue->ProcessTasks(nullptr, 0), "should have tasks"))
      return false;
    if (!check(!queue->ProcessTasks(nullptr, 0), "should have no tasks"))
      return false;
    if (!check(sTasksRan == 3, "3 tasks should have been run"))
      return false;

    sTasksRan = 0;
    struct timeval tv = {0, 0};
    queue->PostTask(new BasicTask());
    queue->PostTask(new BasicTask());
    if (!check(queue->ProcessTasks(&tv, 0), "should process one task"))
      return false;
    if (!check(sTasksRan == 1, "1 task should have been run"))
      return false;
    if (!check(queue->ProcessTasks(&tv, 0), "should process one task"))
      return false;
    if (!check(sTasksRan == 2, "1 task should have been run"))
      return false;

    queue->PostTask(new BasicTask());
    queue->PostTask(new BasicTask());
    queue->PostTask(new BasicTask());

    sTasksRan = 0;
    if (!check(queue->ProcessTasks(nullptr, 2), "should process tasks"))
      return false;
    if (!check(sTasksRan == 2, "should have ran 2 tasks"))
      return false;

    return true;
  }

  bool test_quit() {
    AutoPtr<TaskQueue> queue(TaskQueue::Create(this));

    sTasksRan = 0;
    queue->PostTask(new BasicTask());
    queue->PostTask(new BasicTask());
    queue->PostTask(new BasicTask());
    queue->PostTask(new DelayedQuit(queue));
    queue->PostTask(new BasicTask());
    queue->PostTask(new BasicTask());
    if (!check(queue->ProcessTasks(nullptr), "should process tasks"))
      return false;
    if (!check(sTasksRan == 3, "should only have processed 4 tasks"))
      return false;
    if (!check(queue->ShouldQuit(), "ShouldQuit() should be true"))
      return false;
    if (!check(gotQuit, "should have received quit notification"))
      return false;
    
    return true;
  }

  bool test_threads() {
    sTaskVar = new ConditionVariable();

    AutoPtr<TaskQueue> queue(TaskQueue::Create(this));
    AutoPtr<ThreadRunner> worker1(new ThreadRunner(queue));

    sTasksRan = 0;
    gotQuit = false;
    AutoPtr<Thread> thread1(new Thread(worker1));

    {
      AutoLock lock(sTaskVar);
      while (true) {
        if (queue->ShouldQuit())
          break;

        if (!hasTasks) {
          sTaskVar->Wait();
          continue;
        }
        hasTasks = false;

        while (true) {
          AutoUnlock unlock(sTaskVar);
          if (!queue->ProcessTasks())
            break;
        }
      }
    }

    if (!check(sTasksRan == 2000, "should have ran 2000 tasks"))
      return false;
    if (!check(gotQuit, "should have gotten a quit message"))
      return false;

    sTaskVar = nullptr;
    return true;
  }

  bool Run() override {
    if (!test_basic())
      return false;
    if (!test_quit())
      return false;
    if (!test_threads())
      return false;
    return true;
  }

  bool hasTasks;
  bool gotQuit;
  size_t notifications;
};

class SetupTaskTests
{
 public:
  SetupTaskTests() {
    Tests.append(new TestTasks());
  }
} sSetupTaskTests;
