// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_time_h_
#define _include_amio_time_h_

#include <stdint.h>
#include <time.h>

namespace amio {

// The high resolution timer attempts to wrap semi-reliable high-resolution
// timers. It is mainly used for rate-limiting task loops. Consumers should
// account for poor resolutions or hardware bugs that cause rollover.
class HighResolutionTimer
{
 public:
  // Returns the resolution of the timer, in nanoseconds. If timing could not
  // be initialized, this returns 0.
  static int64_t Resolution();

  // Returns a time counter in nanoseconds.
  static int64_t Counter();
};

static const int64_t kNanosecondsPerMicrosecond = 1000;
static const int64_t kNanosecondsPerMillisecond = 1000000;
static const int64_t kNanosecondsPerSecond = 1000000000;
static const int64_t kMicrosecondsPerMillisecond = 1000;
static const int64_t kMicroSecondsPerSecond = 1000000;
static const int64_t kMillisecondsPerSecond = 1000;

}
#endif // _include_amio_time_h_
