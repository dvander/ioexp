// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <amio.h>
#include <amio-net.h>
#include <amio-time.h>
#include "testing.h"

using namespace ke;

ke::Vector<Ref<Test>> ke::Tests;
ke::Vector<AutoTestContext *> ke::TestContexts;

int main(int argc, char **argv)
{
#if defined(KE_POSIX)
  AutoDisableSigpipe disable_sigpipe;
#elif defined(KE_WINDOWS)
  if (Ref<amio::IOError> error = amio::net::StartNetworking()) {
    fprintf(stderr, "Could not start networking: %s\n", error->Message());
    return 1;
  }
#endif

  SetupTests();

  int64_t res = HighResolutionTimer::Resolution();
  if (res < kNanosecondsPerMicrosecond)
    printf("Timer resolution: " KE_I64_FMT "ns\n", res);
  else if (res < kNanosecondsPerMillisecond)
    printf("Timer resolution: " KE_I64_FMT "us\n", res / kNanosecondsPerMicrosecond);
  else
    printf("Timer resolution: " KE_I64_FMT "ms\n", res / kNanosecondsPerMillisecond);
  printf("Time: " KE_I64_FMT "\n", HighResolutionTimer::Counter());

  bool ok = true;
  for (size_t i = 0; i < Tests.length(); i++) {
    Ref<Test> test = Tests[i];
    if (argc >= 2 && strcmp(argv[1], test->name()) != 0)
      continue;
    fprintf(stdout, "Testing %s... \n", test->name());
    if (!test->Run()) {
      ok = false;
      fprintf(stdout, "TEST: %s FAIL\n", test->name());
      break;
    }
    fprintf(stdout, "TEST: %s OK\n", test->name());

    // Kill the test.
    Tests[i] = nullptr;
  }
  if (ok)
    fprintf(stdout, "All tests passed!\n");
}

#if defined(__GNUC__)
extern "C" void __cxa_pure_virtual()
{
  abort();
}
#endif
