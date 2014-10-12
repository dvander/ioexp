// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include <amio-windows.h>
#include "../shared/amio-errors.h"
#include "amio-windows-errors.h"
#include "amio-windows-util.h"
#include "amio-windows-file.h"
#include "amio-windows-socket.h"
#include "amio-windows-iocp.h"
#include <WinSock2.h>

using namespace amio;
using namespace ke;

CancelIoEx_t amio::gCancelIoEx;
SetFileCompletionNotificationModes_t amio::gSetFileCompletionNotificationModes;

class InitFunctions
{
 public:
  InitFunctions() {
    HMODULE kernel32 = LoadLibraryA("Kernel32.dll");
    if (!kernel32)
      return;

    gSetFileCompletionNotificationModes =
      (SetFileCompletionNotificationModes_t)GetProcAddress(kernel32, "SetFileCompletionNotificationModes");
    gCancelIoEx = (CancelIoEx_t)GetProcAddress(kernel32, "CancelIoEx");

    FreeLibrary(kernel32);
  }
} sInitFunctions;

PassRef<IOError>
amio::EnableImmediateDelivery(HANDLE handle)
{
  if (!gSetFileCompletionNotificationModes)
    return eImmediateDeliveryNotSupported;
  if (!SetFileCompletionNotificationModes(handle, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS))
    return new WinError();
  return nullptr;
}

PassRef<IOError>
PollerFactory::CreatePoller(Ref<Poller> *poller)
{
  return CreateCompletionPort(poller, 1);
}

PassRef<IOError>
PollerFactory::CreateCompletionPort(Ref<Poller> *poller, size_t numConcurrentThreads)
{
  Ref<CompletionPort> port = new CompletionPort();
  Ref<IOError> error = port->Initialize(numConcurrentThreads);
  if (error)
    return error;
  *poller = port;
  return nullptr;
}

Ref<GenericError> eInvalidFlags = new GenericError("invalid flags");

PassRef<IOError>
TransportFactory::CreateFromFile(Ref<Transport> *outp, HANDLE handle, TransportFlags flags)
{
  if (flags & ~(kTransportNoAutoClose))
    return eInvalidFlags;
  *outp = new FileTransport(handle, flags);
  return nullptr;
}

PassRef<IOError>
TransportFactory::CreateFromSocket(Ref<Transport> *outp, SOCKET socket, TransportFlags flags)
{
  if (flags & ~(kTransportNoAutoClose))
    return eInvalidFlags;
  *outp = new SocketTransport(socket, flags);
  return nullptr;
}
