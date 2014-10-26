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
#include "posix-errors.h"

using namespace ke;
using namespace amio;

static int64_t sTimerResolution;

static inline int64_t
timespec_to_int64(const struct timespec &ts)
{
  return ts.tv_nsec + int64_t(ts.tv_sec) * kNanosecondsPerSecond;
}

class DetermineTimerResolution
{
 public:
  DetermineTimerResolution() {
    struct timespec rv;
    if (clock_getres(CLOCK_MONOTONIC, &rv) == -1) {
      Ref<PosixError> error = new PosixError();
      fprintf(stderr, "Could not determine clock resolution: %s\n", error->Message());
      return;
    }
    sTimerResolution = timespec_to_int64(rv);
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
  struct timespec rv;
  if (clock_gettime(CLOCK_MONOTONIC, &rv) == -1) {
    Ref<PosixError> error = new PosixError();
    fprintf(stderr, "Failed to read clock: %s\n", error->Message());
    return 0;
  }
  return timespec_to_int64(rv);
}
