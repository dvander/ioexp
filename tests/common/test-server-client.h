// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_test_server_client_h_
#define _include_amio_test_server_client_h_

#include <amio.h>
#include <amio-net.h>
#include "../testing.h"

namespace amio {

class TestServerClient : public Test
{
 public:
  TestServerClient(CreatePoller_t constructor, const char *name);

  bool Run() override;

 private:
  CreatePoller_t constructor_;
  Ref<Poller> poller_;
};

}

#endif // _include_amio_test_server_client_h_
