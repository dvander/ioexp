// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
#include "amio-errors.h"
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
SelectImpl::Attach(Ref<Socket> baseSocket, Ref<SocketListener> listener)
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
  fds_[slot].watching_writes = false;
  fds_[slot].watching_writes = false;
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
    if (fds_[i].watching_reads)
      FD_SET(fds_[i].socket->Handle(), &read_fds_);
    if (fds_[i].watching_writes)
      FD_SET(fds_[i].socket->Handle(), &write_fds_);
  }

  timeval timeout;
  timeval *timeoutp = nullptr;
  if (timeoutMs != kNoTimeout) {
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    timeoutp = &timeout;
  }

  generation_++;
  int rv = select(0, &read_fds_, &write_fds_, nullptr, timeoutp);
  if (rv == SOCKET_ERROR)
    return new WinError(WSAGetLastError());

  assert(rv >= 0);
  for (size_t i = 0; i < fds_.length() && rv >= 0; i++) {
    if (fds_[i].modified == generation_)
      continue;

    if (fds_[i].watching_reads && FD_ISSET(fds_[i].socket->Handle(), &read_fds_)) {
      fds_[i].watching_reads = false;
      fds_[i].socket->listener()->OnReadReady(fds_[i].socket);
      if (fds_[i].modified == generation_)
        continue;
    }

    if (fds_[i].watching_writes && FD_ISSET(fds_[i].socket->Handle(), &write_fds_)) {
      fds_[i].watching_writes = false;
      fds_[i].socket->listener()->OnWriteReady(fds_[i].socket);
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
  fds_[slot].watching_reads = true;
}

PassRef<IOError>
SelectImpl::onWriteWouldBlock(WinSocket *socket)
{
  assert(socket->poller() == this);
  assert(!socket->Closed());
  size_t slot = socket->userData();
  fds_[slot].watching_writes = true;
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