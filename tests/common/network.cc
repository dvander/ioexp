// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include <amio.h>
#include <amio-net.h>
#include "../testing.h"

using namespace ke;
using namespace amio;

class NetworkTests : public Test
{
 public:
  NetworkTests(const char *name)
   : Test(name)
  {}
  bool Run() override {
    if (!resolve_ipv4())
      return false;
    if (!resolve_ipv6())
      return false;
    if (!resolve_unix())
      return false;
    return true;
  }

  bool resolve_ipv4() {
    Ref<IOError> error;
    Ref<net::IPv4Address> address;
    if (!check_error(net::IPv4Address::Resolve(&address, "localhost"), "resolve localhost on ipv4"))
      return false;
   
    AString name = address->ToString();
    if (!check(name.compare("127.0.0.1") == 0, "address should be 127.0.0.1")) {
      print_actual("%s", name.chars());
      return false;
    }

    if (!check_error(net::IPv4Address::Resolve(&address, "localhost:80"), "resolve localhost:80 on ipv4"))
      return false;
    name = address->ToString();
    if (!check(name.compare("127.0.0.1:80") == 0, "address should be 127.0.0.1:80")) {
      print_actual("%s", name.chars());
      return false;
    }

#if !defined(KE_SOLARIS)
    // These tests do not work on Solaris.
    if (!check_error(net::IPv4Address::Resolve(&address, "localhost:http"), "resolve localhost:http on ipv4"))
      return false;
    name = address->ToString();
    if (!check(name.compare("127.0.0.1:80") == 0, "address should be 127.0.0.1:80")) {
      print_actual("%s", name.chars());
      return false;
    }
#endif

    if (!check_error(net::IPv4Address::Resolve(&address, "127.0.0.1:80"), "resolve 127.0.0.1:80 on ipv4"))
      return false;
    name = address->ToString();
    if (!check(name.compare("127.0.0.1:80") == 0, "address should be 127.0.0.1:80")) {
      print_actual("%s", name.chars());
      return false;
    }

    return true;
  }

  bool resolve_ipv6() {
    Ref<IOError> error;
    Ref<net::IPv6Address> address;
    if (!check_error(net::IPv6Address::Resolve(&address, "localhost"), "resolve localhost on ipv6"))
      return false;
   
    AString name = address->ToString();
    if (!check(name.compare("::1") == 0, "address should be ::1")) {
      print_actual("%s", name.chars());
      return false;
    }

    if (!check_error(net::IPv6Address::Resolve(&address, "[localhost]:80"), "resolve [localhost]:80 on ipv6"))
      return false;
    name = address->ToString();
    if (!check(name.compare("[::1]:80") == 0, "address should be [::1]:80")) {
      print_actual("%s", name.chars());
      return false;
    }

    if (!check_error(net::IPv6Address::Resolve(&address, "::1"), "resolve 0:0:0:0:0:0:0:1 on ipv6"))
      return false;
    name = address->ToString();
    if (!check(name.compare("::1") == 0, "address should be ::1")) {
      print_actual("%s", name.chars());
      return false;
    }

    if (!check_error(net::IPv6Address::Resolve(&address, "[::1]:80"), "resolve [::1]:80 on ipv6"))
      return false;
    name = address->ToString();
    if (!check(name.compare("[::1]:80") == 0, "address should be [::1]:80")) {
      print_actual("%s", name.chars());
      return false;
    }

    return true;
  }

  bool resolve_unix() {
#if defined(KE_POSIX)
    Ref<net::UnixAddress> address;
    if (!check_error(net::UnixAddress::Resolve(&address, "/tmp/tmp.sock"), "resolve /tmp/tmp.sock"))
      return false;
   
    AString name = address->ToString();
    if (!check(name.compare("/tmp/tmp.sock") == 0, "address should be /tmp/tmp.sock")) {
      print_actual("%s", name.chars());
      return false;
    }
#endif
    
    return true;
  }
};

void
ke::SetupNetworkTests()
{
  Tests.append(new NetworkTests("basic-net"));
}
