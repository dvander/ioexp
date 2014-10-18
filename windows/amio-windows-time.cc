// vim: set sts=2 ts=8 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include <amio-windows.h>
#include <amio-time.h>
#include <am-thread-utils.h>
#include "amio-windows-errors.h"
#include <time.h>
#include <stdio.h>
#include <Mmsystem.h>

using namespace ke;
using namespace amio;

// Default is 10ms, if we can't use QPC.
static int64_t sMicrosecondsPerTick;

// Apparently QPC is buggy on Athlon X2.
// http://amd-dev.wpengine.netdna-cdn.com/wordpress/media/2012/10/TSC_Dual-Core_Utility.pdf
static inline bool
IsBuggyAthlon()
{
#if defined(_M_X86) || !defined(_M_X64)
  int regs[4];
  __cpuid(regs, 0);

  char vendor[32];
  memset(vendor, 0, sizeof(vendor));
  *reinterpret_cast<int *>(vendor) = regs[1];
  *reinterpret_cast<int *>(vendor + 4) = regs[3];
  *reinterpret_cast<int *>(vendor + 8) = regs[2];
  if (strcmp(vendor, "AuthenticAMD") != 0)
    return false;

  __cpuid(regs, 1);
  int family = (regs[0] >> 8) & 0xf;
  int ext_family = (regs[0] >> 20) & 0xff;
  if (family == 0xF && ext_family == 0)
    return true;

  return false;
#else
  return false;
#endif
}

class WinTimeUtil
{
 public:
  WinTimeUtil() {
    if (!init()) {
      last_tick_ = timeGetTime();
      rollover_time_ = 0;
      rollover_lock_ = new Mutex();
    }
  }

  int64_t GetLowResCounter() {
    // Try to automatically account for the 49.7 day rollover of timeGetTime.
    AutoLock lock(rollover_lock_);
    DWORD now = timeGetTime();
    if (now < last_tick_)
      rollover_time_ += (int64_t(1 << 31) - 1) * kNanosecondsPerMillisecond;
    last_tick_ = now;
    return (last_tick_ * kNanosecondsPerMillisecond) + rollover_time_;
  }

 private:
  bool init() {
    if (IsBuggyAthlon()) {
      fprintf(stderr, "Note: disabling high-performance query counters due to buggy Athlon X2 CPU\n");
      return false;
    }

    LARGE_INTEGER lv;
    if (!QueryPerformanceFrequency(&lv)) {
      Ref<WinError> error = new WinError();
      fprintf(stderr, "Could not use high-performance query counters: %s\n", error->Message());
      return false;
    }

    sMicrosecondsPerTick = lv.QuadPart / kMicrosecondsPerSecond;
    assert(sMicrosecondsPerTick > 0);

    return sMicrosecondsPerTick > 0;
  }

 private:
  DWORD last_tick_;
  int64_t rollover_time_;
  AutoPtr<Mutex> rollover_lock_;

} sWinTimeUtil;

int64_t
HighResolutionTimer::Resolution()
{
  if (sMicrosecondsPerTick)
    return sMicrosecondsPerTick * kNanosecondsPerMicrosecond;

  // Get the midpoint between timeGetTime() and GetTickCount().
  return 10 * kNanosecondsPerMillisecond;
}

int64_t
HighResolutionTimer::Counter()
{
  if (!sMicrosecondsPerTick)
    return sWinTimeUtil.GetLowResCounter();

  LARGE_INTEGER lv;
  if (!QueryPerformanceCounter(&lv)) {
    Ref<WinError> error = new WinError();
    fprintf(stderr, "Could not query performance counters: %s\n", error->Message());
    return 0;
  }

  return lv.QuadPart / sMicrosecondsPerTick;
}
