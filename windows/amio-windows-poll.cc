// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
#include "../shared/amio-errors.h"
#include "amio-windows-errors.h"
#include "amio-windows-socket.h"
#include "amio-windows-poll.h"

using namespace amio;
using namespace ke;

typedef int (WSAAPI *WSAPoll_t)(_Inout_ WSAPOLLFD[], _In_ ULONG, _In_ INT);
static WSAPoll_t fnWSAPoll;
static bool sCheckedWSAPoll;

bool
amio::IsWSAPollAvailable()
{
  if (sCheckedWSAPoll)
    return !!fnWSAPoll;

  sCheckedWSAPoll = true;

  HMODULE lib = LoadLibraryA("Ws2_32.dll");
  if (!lib)
    return false;
  fnWSAPoll = (WSAPoll_t)GetProcAddress(lib, "WSAPoll");
  FreeLibrary(lib);

  return !!fnWSAPoll;
}

PollImpl::PollImpl()
 : generation_(0)
{
  assert(IsWSAPollAvailable());
}

PollImpl::~PollImpl()
{
  for (size_t i = 0; i < fds_.length(); i++) {
    if (fds_[i].socket) {
      fds_[i].socket->detach();
      fds_[i].socket = nullptr;
    }
  }
}

PassRef<IOError>
PollImpl::Attach(Ref<Socket> baseSocket, Ref<SocketListener> listener, EventFlags eventMask)
{
  WinSocket *socket = baseSocket->toWinSocket();
  if (!socket)
    return eIncompatibleSocket;
  if (socket->Closed())
    return eSocketClosed;
  if (socket->poller())
    return eSocketAlreadyAttached;

  assert(listener);

  size_t slot;
  if (free_slots_.empty()) {
    assert(fds_.length() == events_.length());
    slot = fds_.length();
    if (!fds_.ensure(fds_.length() + 1) || !events_.ensure(events_.length() + 1))
      return eOutOfMemory;
    fds_.infallibleAppend(PollData());
    events_.infallibleAppend(WSAPOLLFD());
  } else {
    slot = free_slots_.popCopy();
    assert(!fds_[slot].socket);
  }

  socket->attach(this, listener);
  socket->setUserData(slot);
  fds_[slot].modified = generation_;
  fds_[slot].socket = socket;
  events_[slot].events = 0;
  if (eventMask & Event_Read)
    events_[slot].events |= POLLIN;
  if (eventMask & Event_Write)
    events_[slot].events |= POLLOUT;
  events_[slot].fd = socket->Handle();
  return nullptr;
}

void
PollImpl::Detach(Ref<Socket> baseSocket)
{
  WinSocket *socket = baseSocket->toWinSocket();
  if (!socket || socket->Closed() || socket->poller() != this)
    return;

  unhook(socket);
}

PassRef<IOError>
PollImpl::Poll(int timeoutMs)
{
  int rv = fnWSAPoll(events_.buffer(), events_.length(), timeoutMs);
  if (rv == SOCKET_ERROR)
    return new WinsockError();
  assert(rv >= 0);

  generation_++;
  for (size_t i = 0; i < events_.length() && rv > 0; i++) {
    if (events_[i].revents == 0)
      continue;

    rv--;
    if (fds_[i].modified == generation_)
      continue;

    if (events_[i].revents & POLLERR) {
      // Get a local copy of the poll data before we wipe it out.
      Ref<WinSocket> socket = fds_[i].socket;
      Ref<SocketListener> listener = socket->listener();
      unhook(socket);
      listener->OnError(socket, eUnknownHangup);
      continue;
    }

    // Prioritize POLLIN over POLLHUP.
    if (events_[i].revents & POLLIN) {
      // Remove the flag to simulate edge-triggering.
      events_[i].events &= ~POLLIN;
      fds_[i].socket->listener()->OnReadReady(fds_[i].socket);
      if (!isEventValid(i))
        continue;
    }

    // Handle hangups.
    if (events_[i].events & POLLHUP) {
      // Get a local copy of the poll data before we wipe it out.
      Ref<WinSocket> socket = fds_[i].socket;
      Ref<SocketListener> listener = socket->listener();
      unhook(socket);
      listener->OnHangup(socket);
      continue;
    }

    // Handle output.
    if (events_[i].events & POLLOUT) {
      // Remove the flag to simulate edge-triggering.
      events_[i].events &= POLLOUT;
      fds_[i].socket->listener()->OnWriteReady(fds_[i].socket);
    }
  }

  return nullptr;
}

void
PollImpl::onReadWouldBlock(WinSocket *socket)
{
  assert(socket->poller() == this);
  assert(!socket->Closed());

  size_t slot = socket->userData();
  assert(events_[slot].fd == socket->Handle());

  if (!(events_[slot].events & POLLIN))
    events_[slot].events |= POLLIN;
}

PassRef<IOError>
PollImpl::onWriteWouldBlock(WinSocket *socket)
{
  return nullptr;
  assert(socket->poller() == this);
  assert(!socket->Closed());

  size_t slot = socket->userData();
  assert(events_[slot].fd == socket->Handle());

  if (!(events_[slot].events & POLLOUT))
    events_[slot].events |= POLLOUT;
}

void
PollImpl::unhook(WinSocket *socket)
{
  assert(socket->poller() == this);
  assert(!socket->Closed());

  size_t slot = socket->userData();
  assert(events_[slot].fd == socket->Handle());

  socket->detach();

  events_[slot].events = 0;
  events_[slot].fd = INVALID_SOCKET;
  fds_[slot].modified = generation_;
  fds_[slot].socket = nullptr;
  free_slots_.append(slot);
}
