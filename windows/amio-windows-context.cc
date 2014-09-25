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
#include "amio-windows-context.h"

using namespace amio;
using namespace ke;

PassRef<IOContext>
IOContext::New(uintptr_t data)
{
  return new WinContext(data);
}

WinContext::WinContext(uintptr_t data)
 : data_(data),
   associated_(false)
{
  memset(&ov_, 0, sizeof(ov_));
}