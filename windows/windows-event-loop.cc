// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "windows-event-loop.h"
#include <stdio.h>

using namespace ke;
using namespace amio;

PassRef<IOError>
EventLoopForIO::Create(Ref<EventLoopForIO> *outp, Ref<Poller> poller)
{
  if (!poller) {
    if (Ref<IOError> error = PollerFactory::Create(&poller))
      return error;
  }

  *outp = new WindowsEventLoopForIO(poller);
  return nullptr;
}

WindowsEventLoopForIO::WindowsEventLoopForIO(Ref<Poller> poller)
 : poller_(poller),
   tasks_(this),
   wakeup_(new Wakeup()),
   received_wakeup_(false)
{
}

void
WindowsEventLoopForIO::Shutdown()
{
  wakeup_ = nullptr;
  poller_ = nullptr;
}

WindowsEventLoopForIO::Wakeup::Wakeup()
{
  context_ = IOContext::New();
}

void
WindowsEventLoopForIO::Wakeup::OnCompleted(IOResult &r)
{
  // Just take the context back.
  context_ = r.context;
}

void
WindowsEventLoopForIO::Wakeup::Signal(Ref<Poller> poller)
{
  // If the context is already in the completion port, then we'll always be
  // able to dequeue something, so we don't need to post a wakeup message.
  if (!context_)
    return;

  if (Ref<IOError> error = poller->Post(context_, this)) {
    fprintf(stderr, "could not post wakeup: %s\n", error->Message());
    return;
  }

  context_ = nullptr;
}

PassRef<Poller>
WindowsEventLoopForIO::GetPoller()
{
  return poller_;
}

PassRef<IOError>
WindowsEventLoopForIO::Attach(Ref<Transport> transport, Ref<IOListener> listener)
{
  return poller_->Attach(transport, listener);
}

void
WindowsEventLoopForIO::PostTask(Task *task)
{
  tasks_.PostTask(task);
}

void
WindowsEventLoopForIO::PostQuit()
{
  tasks_.PostQuit();
}

bool
WindowsEventLoopForIO::ShouldQuit()
{
  return tasks_.ShouldQuit();
}

void
WindowsEventLoopForIO::Loop()
{
  while (!ShouldQuit()) {
    if (tasks_.ProcessNextTask())
      continue;

    received_wakeup_ = false;

    if (Ref<IOError> error = poller_->PollOne()) {
      fprintf(stderr, "Could not poll: %s\n", error->Message());
      continue;
    }

    if (received_wakeup_)
      continue;
  }
}

void
WindowsEventLoopForIO::NotifyTask()
{
  wakeup_->Signal(poller_);
}

void
WindowsEventLoopForIO::NotifyQuit()
{
  wakeup_->Signal(poller_);
}