// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_task_queue_h_
#define _include_amio_task_queue_h_

#include <amio-eventloop.h>
#include <am-thread-utils.h>
#include <am-deque.h>

namespace amio {

using namespace ke;

class TaskQueueImpl : public TaskQueue
{
 public:
  TaskQueueImpl(Delegate *delegate);
  ~TaskQueueImpl();

  void PostTask(Task *task) override;
  void PostQuit() override;
  bool ProcessNextTask() override;
  bool ProcessTasks(struct timeval *timelimitp, size_t nlimit) override;
  void Break() override;
  bool ShouldQuit() override {
    return got_quit_;
  }

 private:
  bool ProcessTasksForTime(struct timeval *timelimitp, size_t nlimit);
  bool ProcessTasks(size_t nlimit);

  bool RefillWorkQueue();

  void ZapQueue(Deque<Task *> *queue);

 private:
  Delegate *delegate_;
  AutoPtr<Mutex> queue_lock_;
  AutoPtr<Deque<Task *>> incoming_;
  AutoPtr<Deque<Task *>> work_;
  int64_t timer_res_;
  volatile bool got_break_;
  volatile bool got_quit_;
};

} // namespace amio

#endif // _include_amio_task_queue_h_
