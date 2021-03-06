// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
#include "windows-errors.h"
#include "windows-context.h"
#include "windows-base-poller.h"
#include "windows-socket.h"
#include "windows-util.h"

using namespace amio;
using namespace ke;

SocketTransport::SocketTransport(SOCKET socket, TransportFlags flags)
 : WinTransport(flags),
   socket_(socket)
{
}

SocketTransport::~SocketTransport()
{
  Close();
}

void
SocketTransport::Close()
{
  if (socket_ == INVALID_SOCKET)
    return;

  closesocket(socket_);
  socket_ = INVALID_SOCKET;
  WinTransport::Close();
}

Ref<GenericError> eOldServiceProviders = new GenericError("non-IFS Winsock Base Service Providers are installed");
Ref<GenericError> eSocketNotAStream = new GenericError("only stream-based sockets can use immediate delivery");

// http://support.microsoft.com/kb/2568167
static bool
CanEnableImmediateSocketDelivery()
{
  static bool checked = false;
  static bool ok = false;

  if (checked)
    return ok;

  // Mark early so we can fail and not re-enter later.
  checked = true;

  DWORD bytes = 16384;
  ke::AutoArray<uint8_t> buffer(new uint8_t[bytes]);
  if (!buffer)
    return false;

  WSAPROTOCOL_INFOA *protocols = (WSAPROTOCOL_INFOA *)&buffer[0];
  int count = WSAEnumProtocolsA(NULL, protocols, &bytes);
  if (count == SOCKET_ERROR) {
    if (WSAGetLastError() != WSAENOBUFS)
      return false;
    buffer = new uint8_t[bytes];
    if (!buffer)
      return false;
    protocols = (WSAPROTOCOL_INFOA *)&buffer[0];
  }
  if ((count = WSAEnumProtocolsA(NULL, protocols, &bytes)) == SOCKET_ERROR)
    return false;
  for (int i = 0; i < count; i++) {
    if (!(protocols[i].dwServiceFlags1 & XP1_IFS_HANDLES))
      return false;
  }

  ok = true;
  return true;
}

// http://blogs.technet.com/b/winserverperformance/archive/2008/06/26/designing-applications-for-high-performance-part-iii.aspx#commentItem375098.
static inline PassRef<IOError>
CheckSocketIsStream(SOCKET socket)
{
  WSAPROTOCOL_INFOA infoa;
  WSAPROTOCOL_INFOW infow;

  int optlen = sizeof(infoa);
  if (getsockopt(socket, SOL_SOCKET, SO_PROTOCOL_INFOA, (char *)&infoa, &optlen) != 0) {
    if (WSAGetLastError() != WSAEFAULT || optlen != sizeof(infow))
  	  return new WinsockError();
  	if (getsockopt(socket, SOL_SOCKET, SO_PROTOCOL_INFOW, (char *)&infow, &optlen) != 0)
  	  return new WinsockError();
    infoa.iSocketType = infow.iSocketType;
  }
  if (infoa.iSocketType != SOCK_STREAM)
    return eSocketNotAStream;
  return nullptr;
}

PassRef<IOError>
SocketTransport::EnableImmediateDelivery()
{
  if (!CanEnableImmediateSocketDelivery())
    return eOldServiceProviders;

  Ref<IOError> error = CheckSocketIsStream(socket_);
  if (error)
    return error;

  error = ::EnableImmediateDelivery((HANDLE)socket_);
  if (error)
    return error;
  return nullptr;
}

bool
SocketTransport::read(IOResult *r, WinBasePoller *poller, WinContext *context, void *buffer, size_t length)
{
  // See note in FileTransport::Read().
  poller->link(context, this, RequestType::Read);

  DWORD flags = 0;
  DWORD bytesRead;
  WSABUF wb;
  wb.buf = (CHAR *)buffer;
  wb.len = length;
  int rv = WSARecv(socket_, &wb, 1, &bytesRead, &flags, context->ov(), NULL);
  DWORD error = (rv == 0) ? 0 : WSAGetLastError();

  if (rv == 0 && (error != WSA_IO_PENDING && error != WSAEMSGSIZE)) {
    poller->unlink(context, this);
    *r = IOResult(new WinError(error), context);
    return false;
  }

  *r = IOResult();
  if (error == WSA_IO_PENDING)
    return true;

  r->bytes = size_t(bytesRead);
  r->completed = true;
  if (error == WSAEMSGSIZE)
    r->truncated = true;
  if (r->bytes == 0 && length)
    r->ended = true;

  // NB: WSAEMSGSIZE is not WSA_IO_PENDING, so it will never queue an item.
  if (ImmediateDelivery() || error == WSAEMSGSIZE) {
    r->context = context;
    poller->unlink(context, this);
  }
  return true;
}

bool
SocketTransport::write(IOResult *r, WinBasePoller *poller, WinContext *context, const void *buffer, size_t length)
{
  // See note in FileTransport::Read().
  poller->link(context, this, RequestType::Write);

  DWORD flags = 0;
  DWORD bytesSent;
  WSABUF wb;
  wb.buf = (CHAR *)buffer;
  wb.len = length;
  int rv = WSASend(socket_, &wb, 1, &bytesSent, flags, context->ov(), NULL);
  DWORD error = (rv == 0) ? 0 : WSAGetLastError();

  if (rv == 0 && error != WSA_IO_PENDING) {
    poller->unlink(context, this);
    *r = IOResult(new WinError(error), context);
    return false;
  }

  *r = IOResult();
  if (error == WSA_IO_PENDING)
    return true;

  r->bytes = size_t(bytesSent);
  r->completed = true;
  if (ImmediateDelivery()) {
    r->context = context;
    poller->unlink(context, this);
  }
  return true;
}

DWORD
SocketTransport::GetOverlappedError(OVERLAPPED *ovp)
{
  DWORD ignore1, ignore2;
  if (!WSAGetOverlappedResult(socket_, ovp, &ignore1, FALSE, &ignore2))
    return WSAGetLastError();
  return 0;
}
