// vim: set sts=2 ts=8 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include <time.h>
#include <stdio.h>
#include <amio-time.h>
#include "../posix/amio-posix-errors.h"
//#include <CoreFoundation/CFDate.h>
#include <mach/mach_time.h>
#include <sys/time.h>

using namespace ke;
using namespace amio;

static int64_t sTimerResolution;
struct mach_timebase_info sTimerInfo;

class DetermineTimerResolution
{
 public:
  DetermineTimerResolution() {
    kern_return_t kr = mach_timebase_info(&sTimerInfo);
    if (kr != KERN_SUCCESS) {
      fprintf(stderr, "High-resolution clock initialization failed: %d\n", kr);
      return;
    }

    sTimerResolution = int64_t(sTimerInfo.numer) / int64_t(sTimerInfo.denom);
  }
} sDetermineTimerResolution;

int64_t
HighResolutionTimer::Resolution()
{
  return sTimerResolution;
}

int64_t
HighResolutionTimer::Counter()
{
  uint64_t now = mach_absolute_time();
  if (sTimerInfo.numer == 1 && sTimerInfo.denom == 1)
    return now;

  return (now * sTimerInfo.numer) / int64_t(sTimerInfo.denom);
}
