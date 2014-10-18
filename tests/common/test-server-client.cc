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
#include "test-server-client.h"

using namespace ke;
using namespace amio;
using namespace amio::net;

TestServerClient::TestServerClient(CreatePoller_t constructor, const char *name)
 : Test(name),
   constructor_(constructor)
{
}

class ServerHelper
 : public Server::Listener,
   public ke::Refcounted<ServerHelper>
{
 public:
  ServerHelper()
  {}

  void AddRef() override {
    ke::Refcounted<ServerHelper>::AddRef();
  }
  void Release() override {
    ke::Refcounted<ServerHelper>::Release();
  }
  Action Accept(Ref<Connection> client) override {
    assert(!Client);
    Client = client;
    return Action::DeferNext;
  }
  void OnError(Ref<IOError> error, Severity severity) override {
    Error = error;
    ErrorLevel = severity;
  }

  Ref<Connection> Client;
  Ref<IOError> Error;
  Severity ErrorLevel;
};

class ClientHelper
 : public Client::Listener,
   public ke::Refcounted<ClientHelper>
{
 public:
  ClientHelper()
   : Terminated(false)
  {}

  void AddRef() override {
    ke::Refcounted<ClientHelper>::AddRef();
  }
  void Release() override {
    ke::Refcounted<ClientHelper>::Release();
  }

  void OnConnect(Ref<Connection> connection) override {
    Conn = connection;
  }
  void OnConnectFailed(Ref<IOError> error) override {
    Terminated = true;
    Error = error;
  }

#if defined(KE_POSIX)
  void OnError(Ref<Transport> transport, Ref<IOError> error) override {
    Terminated = true;
    Error = error;
  }
  void OnHangup(Ref<Transport> transport) override {
    Terminated = true;
  }
#endif

  bool Terminated;
  Ref<IOError> Error;
  Ref<Connection> Conn;
};

bool
TestServerClient::Run()
{
  if (!check_error(constructor_(&poller_), "create poller"))
    return false;

  Ref<net::IPv4Address> local;
  if (!check_error(net::IPv4Address::Resolve(&local, "127.0.0.1"), "resolve 127.0.0.1 on ipv4"))
    return false;

  Ref<ServerHelper> srv_helper = new ServerHelper();
  Ref<Server> server;
  if (!check_error(Server::Create(&server, poller_, local, Protocol::TCP, srv_helper),
                   "create tcp server on any port"))
  {
    return false;
  }

  Ref<IPAddress> address = server->ListenAddress()->toIPAddress();
  if (!check(address->Port() != 0, "local port should not be 0"))
    return false;

  Ref<ClientHelper> cli_helper = new ClientHelper();
  Client::Result client;
  if (!check_error(
         Client::Create(
           &client, poller_,
           address, Protocol::TCP,
           cli_helper, Events::None, EventMode::ETS),
      "create client"))
  {
    return false;
  }

  if (!client.connection) {
    if (!check(client.operation != nullptr, "should have an op ptr"))
      return false;
    if (!check_error(poller_->Poll(), "initial poll"))
      return false;

    if (!cli_helper->Conn) {
      if (!check(!cli_helper->Terminated, "should not get connect failed"))
        return false;
    }

    // If we didn't get both events, try for another poll.
    if (!cli_helper->Conn || !srv_helper->Client) {
      if (!check_error(poller_->Poll(), "additional poll"))
        return false;
    }

    if (!check(cli_helper->Conn != nullptr, "client should get a connect event"))
      return false;
  }

  // Make sure we got both events.
  if (!check(srv_helper->Client != nullptr, "server should get an accept event"))
    return false;

  server->Close();

  // Try to connect again. We should get an error.
  cli_helper = new ClientHelper();
  if (!Client::Create(&client, poller_, address, Protocol::TCP, cli_helper)) {
    if (!check(client.connection == nullptr, "should not have connected"))
      return false;

    if (!check_error(poller_->Poll(), "poll for error"))
      return false;

    if (!check(cli_helper->Terminated, "should have a terminated connection"))
      return false;
    if (!check(cli_helper->Error != nullptr, "should have gotten an error"))
      return false;
  }

  return true;
}
