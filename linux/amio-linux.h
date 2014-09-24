// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_linux_h_
#define _include_amio_linux_h_

#include "include/amio.h"

namespace amio {

bool GetLinuxVersion(int *major, int *minor, int *release);

static inline bool
IsAtLeastLinux(int major, int minor, int release)
{
  int maj, min, rel;
  if (!GetLinuxVersion(&maj, &min, &rel))
    return false;
  if (maj > major ||
      (maj == major && min > minor) ||
      (maj == major && min == minor && rel >= release))
  {
    return true;
  }
  return false;
}

} // namespace amio

#endif // _include_amio_linux_h_
