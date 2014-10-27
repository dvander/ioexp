// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "posix-event-loop.h"
#include "posix-event-queue.h"

using namespace ke;
using namespace amio;

PassRef<IOError>
EventLoopForIO::Create(Ref<EventLoopForIO> *outp, Ref<Poller> poller)
{
  if (!poller) {
    if (Ref<IOError> error = PollerFactory::Create(&poller))
      return error;
  }

  Ref<PosixEventLoopForIO> pump(new PosixEventLoopForIO(poller));
  if (Ref<IOError> error = pump->Initialize())
    return error;

  *outp = pump;
  return nullptr;
}

PosixEventLoopForIO::PosixEventLoopForIO(Ref<Poller> poller)
 : poller_(poller),
   received_wakeup_(false)
{
  assert(poller_);
  tasks_ = new TaskQueueImpl(this);
  wakeup_ = new Wakeup(this);
  event_queue_ = new EventQueueImpl(poller_);
}

PosixEventLoopForIO::~PosixEventLoopForIO()
{
  Shutdown();
}

Ref<IOError>
PosixEventLoopForIO::Initialize()
{
  if (Ref<IOError> error = TransportFactory::CreatePipe(&read_pipe_, &write_pipe_))
    return error;

  // The read pipe is level-triggered so that we don't have to read the entire
  // pipe on each poll, but the write pipe is edge-triggered so we don't wakeup
  // spuriously.
  if (Ref<IOError> error = poller_->Attach(read_pipe_, wakeup_, Events::Read, EventMode::Level))
    return error;
  if (Ref<IOError> error = poller_->Attach(write_pipe_, wakeup_, Events::Write, EventMode::ETS))
    return error;
  return nullptr;
}

void
PosixEventLoopForIO::PostTask(Task *task)
{
  tasks_->PostTask(task);
}

void
PosixEventLoopForIO::PostQuit()
{
  tasks_->PostQuit();
}

bool
PosixEventLoopForIO::ShouldQuit()
{
  return tasks_->ShouldQuit();
}

void
PosixEventLoopForIO::Loop()
{
  AutoDisableSigPipe disable_sigpipe;

  while (!ShouldQuit()) {
    // If this starves the event loop, we have bigger problems.
    if (tasks_->ProcessNextTask())
      continue;

    // Reset the received_wakeup_ flag before entering Poll(), so we can tell
    // whether or not we woke up due to needing another task.
    received_wakeup_ = false;

    if (Ref<IOError> error = poller_->Poll()) {
      fprintf(stderr, "Could not poll: %s\n", error->Message());
      continue;
    }

    if (received_wakeup_)
      continue;

    event_queue_->DispatchEvents();
  }
}

void
PosixEventLoopForIO::Wakeup::OnReadReady()
{
  if (!parent_)
    return;
  parent_->OnWakeup();
}

void
PosixEventLoopForIO::OnWakeup()
{
  char buffer[1] = {0};
  IOResult r;
  if (!read_pipe_->Read(&r, buffer, sizeof(buffer)))
    fprintf(stderr, "Could not read after wakeup: %s\n", r.error->Message());

  // We attach the pipe directly to the poller, not to the event queue, so
  // we don't need to do anything here other than tell our caller that a
  // new task is available (rather than test the waters by calling
  // ProcessNextTask()).
  received_wakeup_ = true;
}

void
PosixEventLoopForIO::NotifyTask()
{
  // If for some reason the pipe failed to write, but the reader is empty,
  // we'll still wake up the event loop via OnWriteReady().
  char buffer[1] = {0};
  IOResult r;
  if (!write_pipe_->Write(&r, buffer, sizeof(buffer)))
    fprintf(stderr, "Could not wakeup: %s\n", r.error->Message());

  // Tell the event dispatcher to stop. This can race and miss the opportunity
  // to break, but that's fine. It's more of a warning shot than anything.
  event_queue_->Break();
}

void
PosixEventLoopForIO::NotifyQuit()
{
  NotifyTask();
}

PassRef<IOError>
PosixEventLoopForIO::Attach(Ref<Transport> transport, Ref<StatusListener> listener, Events events, EventMode mode)
{
  return event_queue_->Attach(transport, listener, events, mode);
}

void
PosixEventLoopForIO::Detach(Ref<Transport> transport)
{
  event_queue_->Detach(transport);
}

PassRef<IOError>
PosixEventLoopForIO::ChangeEvents(Ref<Transport> transport, Events events)
{
  return event_queue_->ChangeEvents(transport, events);
}

PassRef<IOError>
PosixEventLoopForIO::AddEvents(Ref<Transport> transport, Events events)
{
  return event_queue_->AddEvents(transport, events);
}

PassRef<IOError>
PosixEventLoopForIO::RemoveEvents(Ref<Transport> transport, Events events)
{
  return event_queue_->RemoveEvents(transport, events);
}

void
PosixEventLoopForIO::Shutdown()
{
  if (!poller_)
    return;

  wakeup_->disable();
  write_pipe_->Close();
  read_pipe_->Close();
  event_queue_->Shutdown();

  // Zap these so they destroy right away.
  event_queue_ = nullptr;
  tasks_ = nullptr;
  poller_ = nullptr;
  wakeup_ = nullptr;
}
