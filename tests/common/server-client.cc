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
#include "server-client.h"

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
  Server::Action Accept(Ref<Transport> client, Ref<Address> address) override {
    assert(!Client);
    Client = client;
    return Server::Action::DeferNext;
  }
  void OnError(Ref<IOError> error, Server::Severity severity) override {
    Error = error;
    ErrorLevel = severity;
  }

  Ref<Transport> Client;
  Ref<IOError> Error;
  Server::Severity ErrorLevel;
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
  }
  void OnConnectFailed(Ref<IOError> error) override {
    Terminated = true;
    Error = error;
  }
  void OnError(Ref<Transport> transport, Ref<IOError> error) override {
    Terminated = true;
    Error = error;
  }
  void OnHangup(Ref<Transport> transport) override {
    Terminated = true;
  }

  void Reset() {
    Conn = nullptr;
    Terminated = false;
    Error = nullptr;
  }

  bool Terminated;
  Ref<IOError> Error;
  Ref<Connection> Conn;
};

bool
TestServerClient::Run()
{
  if (!check_error(constructor_(&poller_), "create poller"))
    return false;

  Ref<IOError> error;
  Ref<net::Address> local = net::IPv4Address::Resolve(&error, "127.0.0.1");
  if (!check_error(error, "resolve 127.0.0.1 on ipv4"))
    return false;

  Ref<ServerHelper> srv_helper = new ServerHelper();
  Ref<Server> server = Server::Create(&error, local, Protocol::TCP, srv_helper);
  if (!check_error(error, "create tcp server on any port"))
    return false;
  if (!check_error(poller_->Attach(server), "attach server to poller"))
    return false;

  Ref<IPAddress> address = server->ListenAddress()->toIPAddress();
  if (!check(address->Port() != 0, "local port should not be 0"))
    return false;

  Ref<ClientHelper> cli_helper = new ClientHelper();
  Ref<Connection> conn = Client::Create(&error, poller_, address, Protocol::TCP, cli_helper);
  if (!check_error(error, "create client"))
    return false;

  if (!conn) {
    if (!check_error(poller_->Poll(), "initial poll"))
      return false;
    // If we didn't get both events, try for another poll.
    if (!cli_helper->Conn || !srv_helper->Client) {
      if (!check_error(poller_->Poll(kSafeTimeout), "additional poll"))
        return false;
    }

    if (!check(cli_helper->Conn, "client should get a connect event"))
      return false;
  }

  // Make sure we got both events.
  if (!check(srv_helper->Client, "server should get an accept event"))
    return false;

  server->Close();

  // Try to connect again. We should get an error.
  conn = Client::Create(&error, poller_, address, Protocol::TCP, cli_helper);
  if (!check_error(error, "create client"))
    return false;

  return true;
}
