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
#include "amio-errors.h"
#include "amio-windows-errors.h"
#include "amio-windows-util.h"
#include "amio-windows-transport.h"
#include "amio-windows-iocp.h"

using namespace amio;
using namespace ke;

#if !defined(FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)
# define FILE_SKIP_COMPLETION_PORT_ON_SUCCESS 0x1
#endif
typedef BOOL (WINAPI *SetFileCompletionNotificationModes_t)(_In_ HANDLE FileHandle, _In_ UCHAR Flags);

PassRef<IOError>
TransportFactory::CreateFromHandle(Ref<Transport> *outp, HANDLE handle, TransportFlags flags)
{
  *outp = new WinTransport(handle, flags);
  return nullptr;
}

static BOOL sCheckedImmediateDelivery = FALSE;
static SetFileCompletionNotificationModes_t fnSetFileCompletionNotificationModes;
static Ref<GenericError> eImmediateDeliveryNotSupported = new GenericError("immediate delivery is not supported");

bool
amio::CanEnableImmediateDelivery()
{
  if (fnSetFileCompletionNotificationModes)
    return true;
  if (sCheckedImmediateDelivery)
    return false;

  sCheckedImmediateDelivery = true;
  HMODULE kernel32 = LoadLibraryA("Kernel32.dll");
  if (!kernel32)
    return false;
  fnSetFileCompletionNotificationModes =
    (SetFileCompletionNotificationModes_t)GetProcAddress(kernel32, "SetFileCompletionNotificationModes");
  FreeLibrary(kernel32);

  return !!fnSetFileCompletionNotificationModes;
}

PassRef<IOError>
amio::EnableImmediateDelivery(HANDLE handle)
{
  if (!CanEnableImmediateDelivery())
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
  if (*error)
    return error;
  *poller = port;
  return nullptr;
}