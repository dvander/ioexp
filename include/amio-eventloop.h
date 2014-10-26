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

// Implement this to post tasks to a TaskQueue.
class AMIO_LINK Task
{
 public:
  // Tasks are freed with |delete|.
  virtual ~Task()
  {}

  // Invoked when the task runs.
  virtual void Run() = 0;

  // Asks the task to cancel. Tasks may ignore cancel requests.
  virtual void Cancel()
  {}

  // AMIO does not call |delete| directly. Instead it calls DeleteMe(), so
  // singleton tasks are possible.
  virtual void DeleteMe() {
    delete this;
  }
};

// A TaskQueue is a fast container for managing tasks that are processed from
// an event loop. Any thread may post tasks to the queue.
class AMIO_LINK TaskQueue
{
 public:
  class AMIO_LINK Delegate
  {
   public:
    // Signal to the underlying event loop that a new message has been sent.
    virtual void NotifyTask() = 0;

    // Signal to the underlying event loop that a quit message has been
    // received.
    virtual void NotifyQuit() = 0;
  };

  // Create a new TaskQueue. The delegate is not owned by the queue. The
  // delegate may be null; if so, the queue does not have a backing
  // mutex and may not be used on other threads.
  static TaskQueue *Create(Delegate *delegate = nullptr);

  // TaskQueues should be freed with |delete|.
  virtual ~TaskQueue()
  {}

  // Post a task to the task queue. This can be called from any thread.
  // Ownership of the task pointer is transferred to the queue.
  virtual void PostTask(Task *task) = 0;

  // Post a special quit message. This tells the task queue to stop processing
  // tasks.
  virtual void PostQuit() = 0;

  // Run at most one task if any tasks are available. Returns whether or not a
  // task was run.
  virtual bool ProcessNextTask() = 0;

  // Run tasks for up to the given amount of time. If the task queue becomes
  // depleted, this will return immediately. If |timelimitp| is null, then
  // all tasks will be depleted.
  //
  // If |nlimit > 0|, then at most |nlimit| tasks will be processed.
  //
  // If |timelimitp| is not null and HighResolutionTimer::Resolution() returns
  // 0, this function will process at most one task, and then immediately exit.
  //
  // Returns whether any tasks were processed. If true, and |timelimitp| is
  // non-null, then it will be filled with the amount of time spent processing
  // tasks.
  virtual bool ProcessTasks(struct timeval *timelimitp = nullptr, size_t nlimit = 0) = 0;

  // Returns true if PostQuit() was called.
  virtual bool ShouldQuit() = 0;

  // If ProcessTasks() is running, it is stopped as soon as possible.
  // Otherwise, this has no effect.
  virtual void Break() = 0;
};

// An event loop encapsulates a TaskQueue and optionally other polling systems.
class AMIO_LINK EventLoop : public TaskQueue::Delegate
{
 public:
  // Event loops should be freed with |delete|.
  virtual ~EventLoop()
  {}

  // Post a task to the event loop. This can be done from any thread. Ownership
  // of the task is transferred to the event loop.
  virtual void PostTask(Task *task) = 0;

  // Post a special message that indicates the event loop should stop
  // immediately.
  virtual void PostQuit() = 0;

  // Returns whether the event loop received a PostQuit().
  virtual bool ShouldQuit() = 0;

  // Polls for new events in a loop. The only way to exit the loop is to post
  // a ShouldQuit() message.
  virtual void Loop() = 0;
};

// An EventQueue is a wrapper around a poller. Instead of immediately
// delivering events, it buffers status changes and can deliver them
// incrementally. This is useful for constructing event loops that
// prioritize some tasks over others.
//
// To use a status queue, simply Poll() and then ask the queue to process
// events.
//
// EventQueues are not thread-safe. All operations must occur on the same
// thread.
class EventQueue : public IODispatcher
{
 public:
  static PassRef<EventQueue> Create(Ref<Poller> poller);

  virtual ~EventQueue()
  {}

  // Process at most one event; returns true if an event was processed.
  virtual bool DispatchNextEvent() = 0;

  // This has effectively the same behavior as TaskQueue::ProcessTasks().
  virtual bool DispatchEvents(struct timeval *timelimitp = nullptr, size_t nlimit = 0) = 0;

  // If DispatchEvents() is in progress, then this breaks out of the loop as
  // soon as possible. Otherwise, it has no effect.
  virtual void Break() = 0;

  // Shutdown the event queue, removing any pending events. After this, no
  // more transports may be attached and no events can be polled. This does
  // not shutdown the underlying poller.
  virtual void Shutdown() = 0;
};

// An event loop for I/O multiplexing. This is essentially a wrapper around
// a Poller and a single EventQueue. Tasks are prioritized over events.
class AMIO_LINK EventLoopForIO
 : public EventLoop,
   public IODispatcher
{
 public:
  // Create an event loop with a Poller. Specify nullptr to create a default poller.
  static PassRef<IOError> Create(EventLoopForIO **outp, Ref<Poller> poller);

  // Return the underlying poller used for this event loop.
  virtual PassRef<Poller> GetPoller() = 0;
};

}

#endif // _include_amio_message_loop_h_
