// vim: set sts=8 ts=2 sw=2 tw=99 et:
//
// Copyright (C) 2013, David Anderson and AlliedModders LLC
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
//  * Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//  * Neither the name of AlliedModders LLC nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef _include_amio_runner_h_
#define _include_amio_runner_h_

#include <stdio.h>
#include <stdarg.h>
#include <am-refcounting.h>
#include <am-vector.h>
#include <amio.h>

namespace ke {

using namespace amio;

class Test : public VirtualRefcounted
{
 public:
  Test(const char *name)
   : name_(name)
  {
  }
  virtual ~Test()
  {}

  virtual bool Run() = 0;

  const char *name() const {
    return name_;
  }

 private:
  const char *name_;
};

class AutoTestContext;
extern ke::Vector<AutoTestContext *> TestContexts;

static inline void
PrintSpaces(FILE *fp, size_t n)
{
  for (size_t i = 0; i < n; i++)
    fprintf(fp, " ");
}

class AutoTestContext
{
 public:
  AutoTestContext(const char *fmt, ...) {
    PrintSpaces(stdout, TestContexts.length() + 1);
    fprintf(stdout, "Testing:");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
    TestContexts.append(this);
  }
  ~AutoTestContext() {
    TestContexts.pop();
  }
};

extern ke::Vector<Ref<Test>> Tests;

static inline bool
check(bool condition, const char *fmt, ...)
{
  FILE *fp = condition ? stdout : stderr;
  PrintSpaces(fp, TestContexts.length() + 1);
  if (condition)
    fprintf(fp, " -- Ok: ");
  else
    fprintf(fp, " -- Failure: ");
  va_list ap;
  va_start(ap, fmt);
  vfprintf(fp, fmt, ap);
  va_end(ap);
  fprintf(fp, "\n");
  return condition;
}

static inline void
print_actual(const char *fmt, ...)
{
  fprintf(stderr, " got: ");
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}

static inline bool
check_error(Ref<IOError> error, const char *fmt, ...)
{
  FILE *fp = error ? stderr : stdout;
  PrintSpaces(fp, TestContexts.length() + 1);
  if (error)
    fprintf(fp, " -- Failure: ");
  else
    fprintf(fp, " -- Ok: ");
  va_list ap;
  va_start(ap, fmt);
  vfprintf(fp, fmt, ap);
  va_end(ap);
  if (error)
    fprintf(fp, " (%s)\n", error->Message());
  else
    fprintf(fp, "\n");
  return !error;
}

// Must be implemented on each platform.
void SetupTests();

typedef PassRef<IOError> (*CreatePoller_t)(Ref<Poller> *outp);

static const int kSafeTimeout = 20;

}

#endif // _include_amio_runner_h_


