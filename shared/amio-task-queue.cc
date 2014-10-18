// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include <assert.h>
#include <amio-time.h>
#include "amio-task-queue.h"

using namespace ke;
using namespace amio;

TaskQueue *
TaskQueue::Create(Delegate *delegate)
{
  return new TaskQueueImpl(delegate);
}

TaskQueueImpl::TaskQueueImpl(Delegate *delegate)
 : delegate_(delegate),
   got_quit_(false)
{
  queue_lock_ = new Mutex();
  incoming_ = new Deque<Task *>();
  work_ = new Deque<Task *>();
  timer_res_ = HighResolutionTimer::Resolution();
}

TaskQueueImpl::~TaskQueueImpl()
{
  ZapQueue(incoming_);
  ZapQueue(work_);
}

void
TaskQueueImpl::ZapQueue(Deque<Task *> *queue)
{
  while (!queue->empty())
    delete queue->popFrontCopy();
}

void
TaskQueueImpl::PostTask(Task *task)
{
  assert(task);

  {
    AutoLock lock(queue_lock_);
    if (!incoming_->append(task))
      delete task;
  }

  // Delegate can't change yet so we don't have grab it during the lock.
  delegate_->NotifyTask();
}

void
TaskQueueImpl::PostQuit()
{
  if (!delegate_)
    return;

  got_quit_ = true;
  delegate_->NotifyQuit();
}

bool
TaskQueueImpl::RefillWorkQueue()
{
  if (!work_->empty())
    return true;

  AutoLock lock(queue_lock_);
  if (incoming_->empty())
    return false;

  AutoPtr<Deque<Task *>> tmp(work_.take());
  work_ = incoming_.take();
  incoming_ = tmp.take();
  return true;
}

bool
TaskQueueImpl::ProcessNextTask()
{
  if (!RefillWorkQueue())
    return false;

  AutoPtr<Task> task(work_->popFrontCopy());
  task->Run();
  return true;
}

bool
TaskQueueImpl::ProcessTasks(struct timeval *timelimitp, size_t nlimit)
{
  if (timelimitp)
    return ProcessTasksForTime(timelimitp, nlimit);
  return ProcessTasks(nlimit);
}

bool
TaskQueueImpl::ProcessTasksForTime(struct timeval *timelimitp, size_t nlimit)
{
  if (!timer_res_) {
    // Process one task and then leave. It's too risky to use timers.
    return ProcessNextTask();
  }

  int64_t maxtime = (timelimitp->tv_sec * kNanosecondsPerSecond) +
                    (timelimitp->tv_usec * kNanosecondsPerMicrosecond);

  // Don't risk taking more time than we have resolution for.
  maxtime -= maxtime % timer_res_;
  assert(maxtime >= 0);

  int64_t start = HighResolutionTimer::Counter();
  int64_t end = start + maxtime;
  int64_t last = start;

  size_t count = 0;
  while (ProcessNextTask()) {
    int64_t now = HighResolutionTimer::Counter();
    if (now >= end || got_quit_)
      break;

    // Check that we haven't exhausted the limit.
    if (nlimit && ++count >= nlimit)
      break;

    // Make sure we don't iloop due to timer bugs.
    if (now < last)
      break;
    last = now;
  }

  return true;
}

bool
TaskQueueImpl::ProcessTasks(size_t limit)
{
  if (!ProcessNextTask())
    return false;

  size_t count = 0;
  do {
    if (limit && (++count >= limit))
      break;
  } while (!got_quit_ && ProcessNextTask());

  return true;
}
