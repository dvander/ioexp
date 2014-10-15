// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_message_loop_h_
#define _include_amio_message_loop_h_

#include <amio.h>
#include <time.h>

namespace amio {

// Implement this to post tasks to the main thread.
class Task
{
 public:
  // Tasks are freed with |delete|.
  virtual ~Task()
  {}

  // Invoked when the task runs.
  virtual void Run() = 0;

  // Asks the task to cancel. By default tasks cannot be cancelled.
  virtual bool Cancel() {
    return false;
  }
};

// A message loop is an abstraction around a poller that provides a general
// message passing and task polling mechanism for inter-thread communication.
class MessageLoop
{
 public:
  // Create a message loop assuming the default poller for the operating system.
  static MessageLoop *Create();

  // Create a message loop with a specific poller.
  static MessageLoop *Create(Ref<Poller> poller);

  // Post a message to the message loop. This can be done from any thread.
  // If the task cannot be added, the task will be deleted immediately.
  virtual PassRef<IOError> PostTask(Task *task) = 0;

  // Post a special message that indicates the loop should stop immediately.
  virtual bool PostQuit() = 0;

  // Returns whether the message loop received a PostQuit().
  virtual bool ShouldQuit() = 0;

  // Poll for messages. This blocks the calling thread until at least one task
  // or I/O event has been processed.
  //
  // This cannot be called from multiple threads nor is it re-entrant.
  virtual PassRef<IOError> RunOnce() = 0;

  // Run Poll() in a loop. This cannot be called from other threads, nor is
  // it re-entrant. The loop will exit once it receives a quit message.
  virtual void Loop() = 0;

  // Poll for new tasks without blocking. If any tasks are available, process
  // them for up to the given amount of time. If maxtime is null, then all
  // tasks are processed. I/O is never polled with this function.
  //
  // Returns the number of tasks performed, and on output the amount of time
  // spent executing them.
  virtual size_t RunTasks(struct timeval *timelimit) = 0;

  // Return the poller this message loop uses. This can be used to manually
  // watch and poll for I/O outside of Loop() or RunOnce().
  virtual Ref<Poller> GetPoller() = 0;
};

}

#endif // _include_amio_message_loop_h_
