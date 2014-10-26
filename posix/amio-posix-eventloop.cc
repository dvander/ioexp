// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "amio-posix-eventloop.h"
#include "amio-posix-eventqueue.h"

using namespace ke;
using namespace amio;

PosixEventLoopForIO::PosixEventLoopForIO(Ref<Poller> poller)
 : poller_(poller)
{
  assert(poller_);
  tasks_ = new TaskQueueImpl(this);
  wakeup_ = new Wakeup(this);
  event_queue_ = new EventQueueImpl(poller_);
}

PosixEventLoopForIO::~PosixEventLoopForIO()
{
  poller_->Detach(read_pipe_);
  poller_->Detach(write_pipe_);
  wakeup_->disable();
  wakeup_ = nullptr;
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
  if (Ref<IOError> error = poller_->Attach(read_pipe_, wakeup_, Events::Write, EventMode::ETS))
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

  for (;;) {
    // If this starves the event loop, we have bigger problems.
    if (tasks_->ProcessNextTask())
      continue;
  }
}

void
PosixEventLoopForIO::NotifyTask()
{
  // If for some reason the pipe failed to write, but the reader is empty,
  // we'll still wake up the event loop via OnWriteReady().
  char buffer[1] = {0};
  IOResult r;
  if (!write_pipe_->Write(&r, buffer, sizeof(buffer))) {
    fprintf(stderr, "Could not wakeup: %s\n", r.error->Message());
    return;
  }
}

void
PosixEventLoopForIO::NotifyQuit()
{
  NotifyTask();
}

PassRef<IOError>
EventLoopForIO::Create(EventLoopForIO **outp, Ref<Poller> poller)
{
  if (!poller) {
    if (Ref<IOError> error = PollerFactory::Create(&poller))
      return error;
  }

  AutoPtr<PosixEventLoopForIO> pump(new PosixEventLoopForIO(poller));
  if (Ref<IOError> error = pump->Initialize())
    return error;

  *outp = pump.take();
  return nullptr;
}
