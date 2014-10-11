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
#include "amio-windows-select.h"
#include "amio-windows-socket.h"

using namespace amio;
using namespace ke;

Ref<GenericError> eMaxSocketsReached = new GenericError("maximum number of sockets reached");

SelectImpl::SelectImpl()
 : generation_(0)
{
}

SelectImpl::~SelectImpl()
{
  for (size_t i = 0; i < fds_.length(); i++) {
    if (fds_[i].socket) {
      fds_[i].socket->detach();
      fds_[i].socket = nullptr;
    }
  }
}

PassRef<IOError>
SelectImpl::Attach(Ref<Socket> baseSocket, Ref<SocketListener> listener, EventFlags eventMask)
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
    if (fds_.length() >= FD_SETSIZE)
      return eMaxSocketsReached;
    if (!fds_.append(PollData()))
      return eOutOfMemory;
  } else {
    slot = free_slots_.popCopy();
  }

  socket->attach(this, listener);
  socket->setUserData(slot);
  fds_[slot].modified = generation_;
  fds_[slot].socket = socket;
  fds_[slot].events = eventMask;
  return nullptr;
}

void
SelectImpl::Detach(Ref<Socket> baseSocket)
{
  WinSocket *socket = baseSocket->toWinSocket();
  if (!socket || socket->Closed() || socket->poller() != this)
    return;

  unhook(socket);
}

PassRef<IOError>
SelectImpl::Poll(int timeoutMs)
{
  FD_ZERO(&read_fds_);
  FD_ZERO(&write_fds_);

  for (size_t i = 0; i < fds_.length(); i++) {
    if (fds_[i].events & Event_Read)
      FD_SET(fds_[i].socket->Handle(), &read_fds_);
    if (fds_[i].events & Event_Write)
      FD_SET(fds_[i].socket->Handle(), &write_fds_);
  }

  timeval timeout;
  timeval *timeoutp = nullptr;
  if (timeoutMs != kNoTimeout) {
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    timeoutp = &timeout;
  }

  int rv = select(0, &read_fds_, &write_fds_, nullptr, timeoutp);
  if (rv == SOCKET_ERROR)
    return new WinsockError();
  assert(rv >= 0);

  generation_++;
  for (size_t i = 0; i < fds_.length() && rv > 0; i++) {
    if ((fds_[i].events & Event_Read) && FD_ISSET(fds_[i].socket->Handle(), &read_fds_)) {
      if (fds_[i].modified == generation_) {
        fds_[i].events &= ~Event_Read;
        fds_[i].socket->listener()->OnReadReady(fds_[i].socket);
      }
      rv--;
    }

    if ((fds_[i].events & Event_Write) && FD_ISSET(fds_[i].socket->Handle(), &write_fds_)) {
      if (fds_[i].modified == generation_) {
        fds_[i].events &= ~Event_Write;
        fds_[i].socket->listener()->OnWriteReady(fds_[i].socket);
      }
      rv--;
    }
  }

  return nullptr;
}

void
SelectImpl::onReadWouldBlock(WinSocket *socket)
{
  assert(socket->poller() == this);
  assert(!socket->Closed());
  size_t slot = socket->userData();
  fds_[slot].events |= Event_Read;
}

PassRef<IOError>
SelectImpl::onWriteWouldBlock(WinSocket *socket)
{
  assert(socket->poller() == this);
  assert(!socket->Closed());
  size_t slot = socket->userData();
  fds_[slot].events |= Event_Write;
  return nullptr;
}

void
SelectImpl::unhook(WinSocket *socket)
{
  assert(socket->poller() == this);
  assert(!socket->Closed());

  size_t slot = socket->userData();
  fds_[slot].modified = generation_;
  fds_[slot].socket = nullptr;
  free_slots_.append(slot);
}
