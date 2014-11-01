// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
#ifndef _include_amio_windows_util_h_
#define _include_amio_windows_util_h_

#include <amio.h>

namespace amio {

#if !defined(FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)
# define FILE_SKIP_COMPLETION_PORT_ON_SUCCESS 0x1
#endif

typedef BOOL (WINAPI *CancelIoEx_t)(
  _In_ HANDLE hFile, 
  _In_opt_ LPOVERLAPPED lpOverlapped
);
typedef BOOL (WINAPI *SetFileCompletionNotificationModes_t)(
  _In_ HANDLE FileHandle,
  _In_ UCHAR Flags
);
typedef BOOL (WINAPI *GetQueuedCompletionStatusEx_t)(
  _In_ HANDLE CompletionPort,
  _Out_writes_to_(ulCount, *ulNumEntriesRemoved) LPOVERLAPPED_ENTRY lpCompletionPortEntries,
  _In_ ULONG ulCount,
  _Out_ PULONG ulNumEntriesRemoved,
  _In_ DWORD dwMilliseconds,
  _In_ BOOL fAlertable
);

PassRef<IOError>
EnableImmediateDelivery(HANDLE handle);

extern CancelIoEx_t gCancelIoEx;
extern SetFileCompletionNotificationModes_t gSetFileCompletionNotificationModes;
extern GetQueuedCompletionStatusEx_t gGetQueuedCompletionStatusEx;


} // namespace amio

#endif // _include_amio_windows_util_h_
