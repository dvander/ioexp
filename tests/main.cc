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
#include "testing.h"

using namespace ke;

ke::Vector<Ref<Test>> ke::Tests;

int main(int argc, char **argv)
{
#if !defined(_WIN32)
  AutoDisableSigpipe disable_sigpipe;
#endif

  SetupTests();
  SetupNetworkTests();

  for (size_t i = 0; i < Tests.length(); i++) {
    Ref<Test> test = Tests[i];
    if (argc >= 2 && strcmp(argv[1], test->name()) != 0)
      continue;
    fprintf(stdout, "Testing %s... \n", test->name());
    if (!test->Run()) {
      fprintf(stdout, "TEST: %s FAIL\n", test->name());
      break;
    }
    fprintf(stdout, "TEST: %s OK\n", test->name());

    // Kill the test.
    Tests[i] = nullptr;
  }
  fprintf(stdout, "All tests passed!\n");
}

#if defined(__GNUC__)
extern "C" void __cxa_pure_virtual()
{
  abort();
}
#endif
