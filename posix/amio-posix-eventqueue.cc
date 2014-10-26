// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "amio-posix-eventqueue.h"

using namespace ke;
using namespace amio;

PassRef<EventQueue>
EventQueue::Create(Ref<Poller> poller)
{
  return new EventQueueImpl(poller);
}

EventQueueImpl::EventQueueImpl(Ref<Poller> poller)
 : poller_(poller)
{
  tasks_ = new TaskQueueImpl(nullptr);
}

EventQueueImpl::~EventQueueImpl()
{
  Shutdown();
}

void
EventQueueImpl::Shutdown()
{
  if (!poller_)
    return;

  while (true) {
    InlineList<Delegate>::iterator first = delegates_.begin();
    if (first == delegates_.end())
      break;

    // Force the status to be dequeued.
    first->events_ &= ~Events::Queued;
    poller_->Detach(first->transport_);
  }

  poller_ = nullptr;
  tasks_ = nullptr;
}

PassRef<IOError>
EventQueueImpl::Attach(Ref<Transport> transport, Ref<StatusListener> listener,
                       Events events, EventMode mode)
{
  Ref<Delegate> delegate = new Delegate(this, transport, listener);
  if (Ref<IOError> error = poller_->Attach(transport, delegate, events, mode|EventMode::Proxy))
    return error;

  delegates_.append(delegate);
  return nullptr;
}

void
EventQueueImpl::Detach(Ref<Transport> transport)
{
  // The API isn't good enough to detect mismatched attach/detach calls on
  // different API. Instead we just perform some dumb checks.
  if (!transport->IsListenerProxying())
    return;

  Ref<StatusListener> listener = transport->Listener();
  if (!listener)
    return;

  // Null the parent out early, to prevent any delegate callbacks from firing.
  Delegate *delegate = static_cast<Delegate *>(*listener);
  delegate->parent_ = nullptr;

  // Detach - this will call Delegate::OnProxyDetach().
  poller_->Detach(delegate->transport_);

  // Finally, zap the delegate.
  remove_delegate(delegate);
}

PassRef<IOError>
EventQueueImpl::ChangeEvents(Ref<Transport> transport, Events events)
{
  return poller_->ChangeEvents(transport, events);
}

PassRef<IOError>
EventQueueImpl::AddEvents(Ref<Transport> transport, Events events)
{
  return poller_->AddEvents(transport, events);
}

PassRef<IOError>
EventQueueImpl::RemoveEvents(Ref<Transport> transport, Events events)
{
  return poller_->RemoveEvents(transport, events);
}

void
EventQueueImpl::remove_delegate(Delegate *delegate)
{
  delegates_.remove(delegate);
  delegate->transport_ = nullptr;
  delegate->forward_ = nullptr;
  delegate->parent_ = nullptr;

  // If we're inside Delegate::Run(), make sure we don't try to double-remove.
  delegate->events_ = Events::None;
}

bool
EventQueueImpl::DispatchNextEvent()
{
  return tasks_->ProcessNextTask();
}

bool
EventQueueImpl::DispatchEvents(struct timeval *timelimitp, size_t nlimit)
{
  return tasks_->ProcessTasks(timelimitp, nlimit);
}

void
EventQueueImpl::Break()
{
  tasks_->Break();
}

void
EventQueueImpl::Delegate::MaybeEnqueue()
{
  if ((events_ & Events::Queued) == Events::Queued)
    return;

  assert(parent_);

  // Note: Manually add a ref for while we're sitting in the task queue. We've
  // overridden DeleteMe() to drop this ref.
  this->AddRef();
  events_ |= Events::Queued;
  parent_->tasks_->PostTask(this);
}

void
EventQueueImpl::Delegate::OnReadReady()
{
  events_ |= Events::Read;
  MaybeEnqueue();
}

void
EventQueueImpl::Delegate::OnWriteReady()
{
  events_ |= Events::Write;
  MaybeEnqueue();
}

void
EventQueueImpl::Delegate::OnHangup(Ref<IOError> error)
{
  events_ |= Events::Hangup;
  error_ = error;
  MaybeEnqueue();
}

void
EventQueueImpl::Delegate::OnProxyDetach()
{
  // If we don't have a parent, it's because the parent is already detaching
  // us, and the underlying poller is signalling that is completing the detach.
  if (!parent_)
    return;

  // Otherwise, the poller is detaching us on its own. If we're enqueued, just
  // set a bit and wait for the task to process.
  if ((events_ & Events::Queued) == Events::Queued) {
    // Clear other events so we don't forward them.
    events_ = Events::Detached | Events::Queued;
    return;
  }

  // Otherwise - we're not enqueued, so zap ourself immediately.
  parent_->remove_delegate(this);
}

void
EventQueueImpl::Delegate::OnChangeProxy(Ref<StatusListener> new_listener)
{
  forward_ = new_listener;
}

void
EventQueueImpl::Delegate::OnChangeEvents(Events new_events)
{
  // Clear any events that we no longer care about.
  events_ &= ~(new_events & (Events::Read|Events::Write));
}

void 
EventQueueImpl::Delegate::Run()
{
  // Remove the queue flag.
  events_ &= ~Events::Queued;

  // If we were enqueued while Detach() was called, we may no longer have a
  // parent.
  if (!parent_)
    return;

  if ((events_ & Events::Read) == Events::Read)
    forward_->OnReadReady();
  if ((events_ & Events::Write) == Events::Write)
    forward_->OnWriteReady();

  if (!!(events_ & (Events::Detached|Events::Hangup))) {
    if ((events_ & Events::Hangup) == Events::Hangup)
      forward_->OnHangup(error_);
    parent_->remove_delegate(this);
  }
}
