// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson, AlliedModders LLC
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include <amio.h>
#include <string.h>
#include "test-pipes.h"

using namespace ke;
using namespace amio;

TestPipes::TestPipes(CreatePoller_t ctor, const char *name, bool uses_select)
 : Test(name),
   constructor_(ctor),
   uses_select_(uses_select)
{
}

bool
TestPipes::setup()
{
  reset();

  Ref<IOError> error = TransportFactory::CreatePipe(&reader_, &writer_);
  if (!check_error(error, "create pipes"))
    return false;

  error = poller_->Attach(reader_, this, Event_Read);
  if (!check_error(error, "attach read pipe"))
    return false;
  error = poller_->Attach(writer_, this, Event_Write);
  if (!check_error(error, "attach write pipe"))
    return false;

  got_read_ = false;
  got_write_ = false;
  got_hangup_ = false;
  return true;
}

void
TestPipes::reset()
{
  if (reader_)
    poller_->Detach(reader_);
  if (writer_)
    poller_->Detach(writer_);
  reader_ = nullptr;
  writer_ = nullptr;
}

bool
TestPipes::Run()
{
  Ref<IOError> error = constructor_(poller_.address());
  if (!check_error(error, "create poller"))
    return false;

  if (!test_read_write())
    return false;
  if (!test_poll_write_close())
    return false;
  if (!test_poll_read_close())
    return false;

  reset();
  poller_ = nullptr;
  return true;
}

bool
TestPipes::wait_for_read()
{
  while (!got_read_) {
    if (!check_error(poller_->Poll(), "poll for read"))
      return false;
    if (!check(!got_hangup_, "should not get hangup"))
      return false;
  }
  return true;
}

bool
TestPipes::wait_for_write()
{
  while (!got_write_) {
    if (!check_error(poller_->Poll(), "poll for write"))
      return false;
    if (!check(!got_hangup_, "should not get hangup"))
      return false;
  }
  return true;
}

bool
TestPipes::test_read_write()
{
  if (!setup())
    return false;

  if (!check_error(poller_->Poll(), "initial poll"))
    return false;
  if (!check(!got_hangup_, "should not receive hangup"))
    return false;
  if (!check(!got_read_, "should not receive read"))
    return false;
  if (!check(got_write_, "should receive write"))
    return false;

  // Write.
  size_t nwritten = 0;
  while (true) {
    IOResult r;
    if (!check(writer_->Write(&r, "hello", 5), "write to pipe"))
      return false;
    nwritten += r.Bytes;
    if (nwritten == 5)
      break;

    got_write_ = false;
    if (!wait_for_write())
      return false;
  }

  // Read.
  size_t nread = 0;
  char buffer[5];
  while (true) {
    if (!wait_for_read())
      return false;

    IOResult r;
    if (!check(reader_->Read(&r, buffer, sizeof(buffer)), "read from pipe"))
      return false;
    if (!r.Completed) {
      got_read_ = false;
      continue;
    }

    nread += r.Bytes;
    if (nread == sizeof(buffer))
      break;
  }
  if (!check(memcmp(buffer, "hello", 5) == 0, "got bytes"))
    return false;

  // Close the other side.
  writer_->Close();
  writer_ = nullptr;

  // We should be ready to read again.
  assert(got_read_);
  IOResult r;
  if (!check(reader_->Read(&r, buffer, sizeof(buffer)), "read from closed pipe"))
    return false;
  if (!check(r.Ended, "should have gotten EOF from read pipe"))
    return false;

  return true;
}

bool
TestPipes::test_poll_write_close()
{
  if (!setup())
    return false;

  writer_->Close();
  if (!check_error(poller_->Poll(), "poll after write close"))
    return false;
  if (!uses_select_) {
    if (!check(got_hangup_, "got hangup"))
      return false;
    if (!check_error(got_error_, "got clean hangup"))
      return false;
    return true;
  }
  if (!check(got_read_, "should have gotten read-ready"))
    return false;

  IOResult r;
  char buffer[1];
  if (!check(reader_->Read(&r, buffer, sizeof(buffer)), "read from closed pipe"))
    return false;
  if (!check(r.Ended, "should have gotten EOF from read pipe"))
    return false;

  return true;
}

bool
TestPipes::test_poll_read_close()
{
  if (!setup())
    return false;

  reader_->Close();
  if (!check_error(poller_->Poll(), "poll after read close"))
    return false;
  if (!uses_select_) {
    if (!check(got_hangup_, "got hangup"))
      return false;
    // We should have gotten an unclean hangup.
    if (!check(got_error_ != nullptr, "got unclean hangup"))
      return false;
    return true;
  }

  // We should get an error trying to write.
  IOResult r;
  char buffer[1] = {0};
  if (!check(!writer_->Write(&r, buffer, sizeof(buffer)), "write to closed pipe"))
    return false;
  if (!check(r.Error != nullptr, "got error"))
    return false;

  return true;
}
