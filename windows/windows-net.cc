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
#include <Mswsock.h>
#include "../shared/shared-errors.h"
#include "../windows/windows-errors.h"
#include "../windows/windows-base-poller.h"
#include "../windows/windows-socket.h"
#include <am-vector.h>
#include <am-thread-utils.h>

using namespace ke;
using namespace amio;
using namespace amio::net;

PassRef<IOError>
net::StartNetworking()
{
  WSADATA data;
  DWORD rv = WSAStartup(MAKEWORD(2, 2), &data);
  if (rv != 0)
    return new WinError(rv);

  if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2)
    return new GenericError("unable to request version 2.2 of WinSock");

  return nullptr;
}

static Ref<IOError>
SocketForAddress(SOCKET *outp, AddressFamily inaf, Protocol protocol)
{
  *outp = INVALID_SOCKET;

  int af;
  switch (inaf) {
    case AddressFamily::IPv4:
      af = AF_INET;
      break;
    case AddressFamily::IPv6:
      af = AF_INET6;
      break;
    default:
      return eUnsupportedAddressFamily;
  }

  int type;
  int proto;
  switch (protocol) {
    case Protocol::TCP:
      if (af != AF_INET && af != AF_INET6)
        return eUnsupportedProtocol;
      type = SOCK_STREAM;
      proto = IPPROTO_TCP;
      break;
    case Protocol::UDP:
      if (af != AF_INET && af != AF_INET6)
        return eUnsupportedProtocol;
      type = SOCK_DGRAM;
      proto = IPPROTO_UDP;
      break;
    case Protocol::Stream:
      proto = 0;
      type = SOCK_STREAM;
      break;
    case Protocol::Datagram:
      proto = 0;
      type = SOCK_DGRAM;
      break;
    default:
      return eUnsupportedProtocol;
  }

  SOCKET sd = WSASocketA(af, type, proto, nullptr, 0, WSA_FLAG_OVERLAPPED);
  if (sd == INVALID_SOCKET)
    return new WinsockError();

  *outp = sd;
  return nullptr;
}

static Ref<IOError>
SocketForAddress(Ref<SocketTransport> *outp, AddressFamily inaf, Protocol protocol)
{
  SOCKET sd;
  if (Ref<IOError> error = SocketForAddress(&sd, inaf, protocol))
    return error;

  *outp = new SocketTransport(sd, kTransportDefaultFlags);
  return nullptr;
}

static inline PassRef<IOError>
BindTo(Ref<SocketTransport> transport, Ref<Address> address)
{
  // Before we bind, set SO_REUSEADDR.
  int enable = 1;
  if (setsockopt(transport->socket(), SOL_SOCKET, SO_REUSEADDR, (const char *)&enable, sizeof(enable)) == -1)
    return new WinsockError();
  if (bind(transport->socket(), address->SockAddr(), address->SockAddrLen()) == -1)
    return new WinsockError();
  return nullptr;
}

class WinConnection
 : public Connection,
   public SocketTransport
{
 public:
  WinConnection(SOCKET socket, Ref<Address> local, Ref<Address> peer)
   : SocketTransport(socket, kTransportDefaultFlags)
  {}

  void AddRef() override {
    SocketTransport::AddRef();
  }
  void Release() override {
    SocketTransport::Release();
  }
  PassRef<Transport> GetTransport() override {
    return this;
  }

  PassRef<IOError> LocalAddress(Ref<Address> *outp) override {
    if (!local_address_) {
      Ref<Address> addr;
      struct sockaddr *buf;
      socklen_t buflen;
      if (Ref<IOError> error = GetAddressBuffer(&addr, &buf, &buflen))
        return error;
      if (getsockname(socket(), buf, &buflen) == SOCKET_ERROR)
        return new WinsockError();
      local_address_ = addr;
    }
    *outp = local_address_;
    return nullptr;
  }
  PassRef<IOError> PeerAddress(Ref<Address> *outp) override {
    if (!peer_address_) {
      Ref<Address> addr;
      struct sockaddr *buf;
      socklen_t buflen;
      if (Ref<IOError> error = GetAddressBuffer(&addr, &buf, &buflen))
        return error;
      if (getpeername(socket(), buf, &buflen) == SOCKET_ERROR)
        return new WinsockError();
      peer_address_ = addr;
    }
    *outp = peer_address_;
    return nullptr;
  }

 private:
  PassRef<IOError> GetAddressBuffer(Ref<Address> *outp, struct sockaddr **addrp, socklen_t *lenp) {
    int af;
    if (Ref<IOError> error = GetAddressFamily(&af))
      return error;
    switch (af) {
      case AF_INET:
        *outp = new IPv4Address(addrp, lenp);
        return nullptr;
      case AF_INET6:
        *outp = new IPv6Address(addrp, lenp);
        return nullptr;
      default:
        return eUnsupportedAddressFamily;
    }
  }
  PassRef<IOError> GetAddressFamily(int *outp) {
    WSAPROTOCOL_INFOA info;
    int size = sizeof(info);
    if (getsockopt(socket(), SOL_SOCKET, SO_PROTOCOL_INFOA, (char *)&info, &size) == SOCKET_ERROR)
      return new WinsockError();

    *outp = info.iAddressFamily;
    return nullptr;
  }

 private:
  Ref<Address> local_address_;
  Ref<Address> peer_address_;
};

struct AcceptRequest
 : public IUserData,
   public ke::RefcountedThreadsafe<AcceptRequest>
{
  Ref<WinConnection> conn;

  // We'd like to just pass an empty buffer to AcceptEx() to reduce complexity,
  // but because of http://support.microsoft.com/kb/973155, we bite the bullet
  // and allocate the addresses ahead of time.
  Ref<Address> local_addr;
  Ref<Address> peer_addr;
  struct sockaddr *local_buf;
  socklen_t local_buflen;
  struct sockaddr *peer_buf;
  socklen_t peer_buflen;
  AutoPtr<uint8_t> buffer;
  size_t buffer_len;

  AcceptRequest(SOCKET socket, Ref<Address> base) {
    local_addr = base->NewBuffer(&local_buf, &local_buflen);
    peer_addr = base->NewBuffer(&peer_buf, &peer_buflen);
    conn = new WinConnection(socket, local_addr, peer_addr);

    // AcceptEx() requires that each address have 16 bytes of padding.
    buffer_len = local_buflen + peer_buflen + 16 * 2;
    buffer = new uint8_t[buffer_len];
  }

  void AddRef() override {
    ke::RefcountedThreadsafe<AcceptRequest>::AddRef();
  }
  void Release() override {
    ke::RefcountedThreadsafe<AcceptRequest>::Release();
  }
};

class WinServer
 : public Server,
   public IOListener,
   public ke::RefcountedThreadsafe<WinServer>
{
 public:
  WinServer(Ref<SocketTransport> transport,
            Ref<Server::Listener> listener,
            Ref<Address> address, Protocol protocol)
   : transport_(transport),
     listener_(listener),
     address_(address),
     protocol_(protocol),
     closing_(false),
     acceptex_(nullptr)
  {
    context_lock_ = new ke::Mutex();
  }
  ~WinServer() {
    Close();
  }

  PassRef<IOError> Setup() {
    DWORD ignore;
    GUID guid = WSAID_ACCEPTEX;
    int rv = WSAIoctl(
      transport_->socket(),
      SIO_GET_EXTENSION_FUNCTION_POINTER,
      &guid, sizeof(guid),
      &acceptex_, sizeof(acceptex_),
      &ignore, nullptr, nullptr);
    if (rv == SOCKET_ERROR)
      return new WinsockError();

    guid = WSAID_GETACCEPTEXSOCKADDRS;
    rv = WSAIoctl(
      transport_->socket(),
      SIO_GET_EXTENSION_FUNCTION_POINTER,
      &guid, sizeof(guid),
      &getAcceptExSockAddrs_, sizeof(getAcceptExSockAddrs_),
      &ignore, nullptr, nullptr);
    if (rv == SOCKET_ERROR)
      return new WinsockError();
    
    // We should really start a request for each polling thread...
    return StartRequest();
  }

  void AddRef() override {
    ke::RefcountedThreadsafe<WinServer>::AddRef();
  }
  void Release() override {
    ke::RefcountedThreadsafe<WinServer>::Release();
  }
  void Close() override {
    if (closing_)
      return;
    transport_->Close();
    closing_ = true;
  }
  PassRef<Address> ListenAddress() override {
    return address_;
  }

  void OnOther(Ref<Transport> transport, IOResult &r) override {
    // Immediately enqueue another request. If this fails we're kind of hosed.
    if (Ref<IOError> error = StartRequest()) {
      listener_->OnError(error, Severity::Fatal);
      return;
    }

    assert(r.completed);
    assert(r.context && r.context->UserData());
    assert(!r.ended && !r.moreData && !r.truncated);

    // Extract the request and then give the context back.
    Ref<AcceptRequest> request = (AcceptRequest *)*r.context->UserData();
    assert(transport_ == transport);

    putContext(r.context);

    if (r.error) {
      listener_->OnError(r.error, Severity::Warning);
      return;
    }

    // Gross...
    SOCKET listen_socket = transport_->socket();
    int rv = setsockopt(
      request->conn->socket(),
      SOL_SOCKET,
      SO_UPDATE_ACCEPT_CONTEXT,
      (char *)&listen_socket,
      sizeof(listen_socket));
    if (rv == SOCKET_ERROR) {
      listener_->OnError(new WinsockError(), Severity::Warning);
      return;
    }

    // Gross.
    struct sockaddr *localp, *peerp;
    socklen_t local_len, peer_len;
    getAcceptExSockAddrs_(
      request->buffer,
      0,                            // dwReceiveDataLength
      request->local_buflen + 16,   // dwLocalAddressLength
      request->peer_buflen + 16,    // dwRemoteAddressLength
      &localp,
      &local_len,
      &peerp,
      &peer_len);

    // Copy the data into their original buffers.
    memcpy(request->local_buf, localp, Min(local_len, request->local_buflen));
    memcpy(request->peer_buf, peerp, Min(peer_len, request->peer_buflen));

    listener_->Accept(request->conn);
  }

 private:
  Ref<IOContext> getContext() {
    {
      ke::AutoLock lock(context_lock_);
      if (!contexts_.empty())
        return contexts_.popCopy();
    }
    return IOContext::New();
  }

  void putContext(Ref<IOContext> context) {
    context->SetUserData(nullptr);

    ke::AutoLock lock(context_lock_);
    contexts_.append(context);
  }

  PassRef<IOError> StartRequest() {
    SOCKET socket;
    if (Ref<IOError> error = SocketForAddress(&socket, address_->Family(), protocol_))
      return error;

    Ref<AcceptRequest> request = new AcceptRequest(socket, address_);
    Ref<IOContext> context = getContext();
    context->SetUserData(request);

    DWORD ignore;
    BOOL rv = acceptex_(
      transport_->socket(),
      request->conn->socket(),
      request->buffer,
      0,                          // dwReceiveDataLength
      request->local_buflen + 16, // dwLocalAddressLength
      request->peer_buflen + 16,  // dwRemoteAddressLength
      &ignore,                    // lpdwBytesReceived
      (WSAOVERLAPPED *)context->LockForOverlappedIO(transport_, RequestType::Other));
    if (!rv) {
      DWORD err = WSAGetLastError();
      if (err != ERROR_IO_PENDING) {
        context->UnlockForFailedOverlappedIO();
        return new WinError(err);
      }
    }

    // Even if the operation succeeds immediately, we'll get an IO notification.
    // For simplicity (and to avoid starving), we just wait for that
    // notification.
    return nullptr;
  }

 private:
  Ref<SocketTransport> transport_;
  Ref<Server::Listener> listener_;
  Ref<Address> address_;
  Protocol protocol_;
  bool closing_;
  LPFN_ACCEPTEX acceptex_;
  LPFN_GETACCEPTEXSOCKADDRS getAcceptExSockAddrs_;

  AutoPtr<Mutex> context_lock_;
  Vector<Ref<IOContext>> contexts_;
};

PassRef<IOError>
Server::Create(Ref<Server> *outp,
               Ref<Poller> poller,
               Ref<Address> address, Protocol protocol,
               Ref<Server::Listener> listener,
               unsigned backlog)
{
  switch (protocol) {
    case Protocol::TCP:
    case Protocol::Stream:
      break;
    default:
      return eUnsupportedProtocol;
  }
  if (!backlog)
    backlog = SOMAXCONN;

  Ref<SocketTransport> transport;
  if (Ref<IOError> error = SocketForAddress(&transport, address->Family(), protocol))
    return error;

  // Bind and listen.
  if (Ref<IOError> error = BindTo(transport, address))
    return error;
  if (listen(transport->socket(), backlog) == -1)
    return new WinsockError();

  struct sockaddr *buf;
  socklen_t buflen;
  Ref<Address> local = address->NewBuffer(&buf, &buflen);
  if (getsockname(transport->socket(), buf, &buflen) == -1)
    return new WinsockError();

  Ref<WinServer> server = new WinServer(transport, listener, local, protocol);
  if (Ref<IOError> error = poller->Attach(transport, server))
    return error;
  if (Ref<IOError> error = server->Setup()) {
    server->Close();
    return error;
  }

  *outp = server;
  return nullptr;
}

// For some god-awful reason this has to be called after using ConnectEx().
static inline PassRef<IOError>
EnableConnectedSocket(SOCKET s)
{
  if (setsockopt(s, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) == SOCKET_ERROR)
    return new WinsockError();
  return nullptr;
}

class ConnectOp
 : public Operation,
   public IOListener,
   public ke::RefcountedThreadsafe<ConnectOp>
{
 public:
  ConnectOp(Ref<WinConnection> conn, Ref<IOContext> context, Ref<Client::Listener> listener)
   : conn_(conn),
     context_(context),
     listener_(listener)
  {
  }

  void AddRef() override {
    ke::RefcountedThreadsafe<ConnectOp>::AddRef();
  }
  void Release() override {
    ke::RefcountedThreadsafe<ConnectOp>::Release();
  }
  void OnOther(Ref<Transport> transport, IOResult &r) override {
    assert(transport == conn_);
    if (r.error) {
      listener_->OnConnectFailed(r.error);
      return;
    }
    if (Ref<IOError> error = EnableConnectedSocket(conn_->socket())) {
      listener_->OnConnectFailed(r.error);
      return;
    }

    conn_->changeListener(listener_);
    listener_->OnConnect(conn_);
  }

  void Cancel() override {
    context_->Cancel();
  }

 private:
  Ref<WinConnection> conn_;
  Ref<IOContext> context_;
  Ref<Client::Listener> listener_;
};

PassRef<IOError>
Client::Create(Result *result,
               Ref<Poller> poller,
               Ref<Address> address,
               Protocol protocol,
               Ref<Client::Listener> listener,
               Events ignoreEvents,
               EventMode ignoreMode)
{
  *result = Result();

  SOCKET socket;
  if (Ref<IOError> error = SocketForAddress(&socket, address->Family(), protocol))
    return error;

  Ref<WinConnection> conn = new WinConnection(socket, nullptr, nullptr);

  DWORD ignore;
  LPFN_CONNECTEX connectEx;
  GUID connectExGuid = WSAID_CONNECTEX;
  int rv = WSAIoctl(
    conn->socket(),
    SIO_GET_EXTENSION_FUNCTION_POINTER,
    &connectExGuid, sizeof(connectExGuid),
    &connectEx, sizeof(connectEx),
    &ignore, nullptr, nullptr);
  if (rv == SOCKET_ERROR)
    return new WinsockError();

  // Aaaand it must be bound already, for some reason. We don't use BindTo()
  // since it specifies SO_REUSEADDR.
  Ref<Address> any;
  if (Ref<IOError> error = Address::AnyAddress(&any, address->Family()))
    return error;
  if (bind(conn->socket(), any->SockAddr(), any->SockAddrLen()) == SOCKET_ERROR)
    return new WinsockError();

  Ref<IOContext> context = IOContext::New();
  Ref<ConnectOp> op = new ConnectOp(conn, context, listener);
  if (Ref<IOError> error = poller->Attach(conn, op))
    return error;

  BOOL ok = connectEx(
    conn->socket(),
    address->SockAddr(),
    address->SockAddrLen(),
    nullptr, // lpSendBuffer
    0,       // dwSendDataLength
    nullptr, // lpdwBytesSent
    context->LockForOverlappedIO(conn, RequestType::Other));
  if (!ok) {
    DWORD err = WSAGetLastError();
    if (err != ERROR_IO_PENDING) {
      context->UnlockForFailedOverlappedIO();
      return new WinError(err);
    }
  } else {
    if (conn->ImmediateDelivery()) {
      if (Ref<IOError> error = EnableConnectedSocket(conn->socket()))
        return error;
      result->connection = conn;
      return nullptr;
    }
  }

  result->operation = op;
  return nullptr;
}

AMIO_LINK Ref<IOError>
net::CreateSocket(Ref<Transport> *outp, AddressFamily af, Protocol proto)
{
  Ref<SocketTransport> transport;
  if (Ref<IOError> error = SocketForAddress(&transport, af, proto))
    return error;

  *outp = transport;
  return nullptr;
}

AMIO_LINK Ref<IOError>
net::CreateSocket(Ref<Transport> *outp, Ref<Address> address, Protocol proto)
{
  Ref<SocketTransport> transport;
  if (Ref<IOError> error = SocketForAddress(&transport, address->Family(), proto))
    return error;
  if (Ref<IOError> error = BindTo(transport, address))
    return error;

  *outp = transport;
  return nullptr;
}

AMIO_LINK Ref<IOError>
net::ConnectTo(Ref<Connection> *outp, Protocol protocol, Ref<Address> address)
{
  SOCKET sd;
  if (Ref<IOError> error = SocketForAddress(&sd, address->Family(), protocol))
    return error;

  Ref<WinConnection> conn = new WinConnection(sd, nullptr, nullptr);
  if (connect(conn->socket(), address->SockAddr(), address->SockAddrLen()) == SOCKET_ERROR)
    return new WinsockError();

  *outp = conn;
  return nullptr;
}
