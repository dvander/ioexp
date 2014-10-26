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
#include <am-string.h>
#include "../posix/amio-posix-errors.h"
#include "../posix/amio-posix-base-poller.h"
#include "../posix/amio-posix-transport.h"
#include "../shared/amio-string.h"
#include <errno.h>
#include <unistd.h>

using namespace ke;
using namespace amio;
using namespace amio::net;

static const size_t kMaxUnixPath = sizeof(struct sockaddr_un) -
                                   offsetof(struct sockaddr_un, sun_path) - 1;
static Ref<GenericError> sUnixNameTooLong = new GenericError("unix name is too long (max: %d)", int(kMaxUnixPath));

UnixAddress::UnixAddress()
{
}

UnixAddress::UnixAddress(struct sockaddr **buf, socklen_t *buflen)
{
  *buf = reinterpret_cast<struct sockaddr *>(&buf_);
  *buflen = sizeof(buf_);
}

PassRef<IOError>
UnixAddress::Resolve(Ref<UnixAddress> *outp, const char *address)
{
  if (strlen(address) > kMaxUnixPath)
    return sUnixNameTooLong;

  Ref<UnixAddress> out = new UnixAddress;
  out->buf_.sun_family = AF_UNIX;
  CopyString(out->buf_.sun_path, sizeof(out->buf_.sun_path), address);
#if defined(SOCKADDR_LEN)
  out->buf_.sun_len = SUN_LEN(&out->buf_);
#endif

  *outp = out;
  return nullptr;
}

socklen_t
UnixAddress::SockAddrLen()
{
  return SUN_LEN(&buf_);
}

AString
UnixAddress::ToString()
{
  return AString(buf_.sun_path);
}

PassRef<Address>
UnixAddress::NewBuffer(sockaddr **outp, socklen_t *lenp)
{
  Ref<UnixAddress> addr = new UnixAddress();
  *outp = reinterpret_cast<sockaddr *>(&addr->buf_);
  *lenp = sizeof(addr->buf_);
  return addr;
}

static Ref<IOError>
SocketForAddress(int *outp, AddressFamily inaf, Protocol protocol)
{
  *outp = -1;

  int af;
  switch (inaf) {
    case AddressFamily::IPv4:
      af = AF_INET;
      break;
    case AddressFamily::IPv6:
      af = AF_INET6;
      break;
    case AddressFamily::Unix:
      af = AF_UNIX;
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

  int fd = socket(af, type, proto);
  if (fd == -1)
    return new PosixError();

  *outp = fd;
  return nullptr;
}

static Ref<IOError>
SocketForAddress(Ref<PosixTransport> *outp, AddressFamily inaf, Protocol protocol)
{
  int fd;
  if (Ref<IOError> error = SocketForAddress(&fd, inaf, protocol))
    return error;

  Ref<PosixTransport> transport = new PosixTransport(fd, kTransportDefaultFlags);
  if (Ref<IOError> error = transport->Setup())
    return error;

  *outp = transport;
  return nullptr;
}

class PosixConnection
 : public Connection,
   public PosixTransport
{
 public:
  PosixConnection(int fd, TransportFlags flags)
   : PosixTransport(fd, flags)
  {}

  void AddRef() override {
    PosixTransport::AddRef();
  }
  void Release() override {
    PosixTransport::Release();
  }
  PassRef<Transport> GetTransport() override {
    return this;
  }
};

template <typename T>
class PosixConnectionT : public PosixConnection
{
 public:
  PosixConnectionT(int fd)
   : PosixConnection(fd, kTransportDefaultFlags)
  {}

  PassRef<IOError> LocalAddress(Ref<Address> *outp) override {
    struct sockaddr *buf;
    socklen_t buflen;
    Ref<T> addr = new T(&buf, &buflen);
    if (getsockname(this->fd(), buf, &buflen) == -1)
      return new PosixError();
    *outp = addr;
    return nullptr;
  }

  PassRef<IOError> PeerAddress(Ref<Address> *outp) override {
    struct sockaddr *buf;
    socklen_t buflen;
    Ref<T> addr = new T(&buf, &buflen);
    if (getpeername(this->fd(), buf, &buflen) == -1)
      return new PosixError();
    *outp = addr;
    return nullptr;
  }
};

class ConnectOp
 : public StatusListener,
   public Operation,
   public ke::RefcountedThreadsafe<ConnectOp>
{
 public:
  ConnectOp(Ref<PosixConnection> conn, Ref<Client::Listener> listener, Events events)
   : conn_(conn),
     listener_(listener),
     events_(events)
  {
  }

  void AddRef() override {
    ke::RefcountedThreadsafe<ConnectOp>::AddRef();
  }
  void Release() override {
    ke::RefcountedThreadsafe<ConnectOp>::Release();
  }
  void Cancel() override {
    // This should throw an error through the poller, which we just ignore.
    conn_->Close();
    Finish();
  }

  // Both of these are unexpected, but will terminate the connect operation
  // anyway.
  void OnHangup(Ref<IOError> error) override {
    if (!error)
      error = eUnknownHangup;
    reportError(eUnknownHangup);
  }

  void OnWriteReady() override {
    int errn;
    socklen_t len = sizeof(errn);
    int rv = getsockopt(conn_->FileDescriptor(), SOL_SOCKET, SO_ERROR, &errn, &len);
    if (rv == -1 || errn != 0) {
      reportError(new PosixError(rv == -1 ? errno : errn));
      return;
    }

    if (Ref<IOError> error = conn_->poller()->ChangeEvents(conn_, events_)) {
      reportError(error);
      return;
    }

    conn_->changeListener(listener_);
    listener_->OnConnect(conn_);
    Finish();
  }

  void reportError(Ref<IOError> error) {
    conn_->Close();
    listener_->OnConnectFailed(error);
    Finish();
  }

  void Finish() {
    conn_ = nullptr;
    listener_ = nullptr;
  }

 private:
  Ref<PosixConnection> conn_;
  Ref<Client::Listener> listener_;
  Events events_;
};

// Note: the fd is automatically closed on error, since we assume the fd has
// not yet moved into an RAII-guarded object.
static inline PassRef<IOError>
ConnectionForSocket(Ref<PosixConnection> *outp, int fd, AddressFamily af)
{
  switch (af) {
    case AddressFamily::IPv4:
      *outp = new PosixConnectionT<IPv4Address>(fd);
      return nullptr;
    case AddressFamily::IPv6:
      *outp = new PosixConnectionT<IPv6Address>(fd);
      return nullptr;
    case AddressFamily::Unix:
      *outp = new PosixConnectionT<UnixAddress>(fd);
      return nullptr;
    default:
      close(fd);
      return eUnsupportedAddressFamily;
  }
}

static inline PassRef<IOError>
ConnectionForAddress(Ref<PosixConnection> *outp, Ref<Address> address, Protocol protocol)
{
  int fd;
  Ref<IOError> error = SocketForAddress(&fd, address->Family(), protocol);
  if (error)
    return error;
  return ConnectionForSocket(outp, fd, address->Family());
}

PassRef<IOError>
Client::Create(Result *result, Ref<IODispatcher> dispatcher,
               Ref<Address> address, Protocol protocol,
               Ref<Client::Listener> listener,
               Events events, EventMode mode)
{
  *result = Result();

  Ref<PosixConnection> conn;
  if (Ref<IOError> error = ConnectionForAddress(&conn, address, protocol))
    return error;
  if (Ref<IOError> error = conn->Setup())
    return error;

  int rv = connect(conn->fd(), address->SockAddr(), address->SockAddrLen());
  if (rv == 0) {
    if (Ref<IOError> error = dispatcher->Attach(conn, listener, events, mode))
      return error;
    result->connection = conn;
    return nullptr;
  }

  Ref<ConnectOp> op = new ConnectOp(conn, listener, events);
  if (Ref<IOError> error = dispatcher->Attach(conn, op, Events::Write, mode))
    return error;

  result->operation = op;
  return nullptr;
}

static inline PassRef<IOError>
BindTo(Ref<PosixTransport> transport, Ref<Address> address)
{
  // Before we bind, set SO_REUSEADDR.
  int enable = 1;
  if (setsockopt(transport->fd(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1)
    return new PosixError();
  if (bind(transport->fd(), address->SockAddr(), address->SockAddrLen()) == -1)
    return new PosixError();
  return nullptr;
}

Ref<IOError> AMIO_LINK
amio::net::CreateSocket(Ref<Transport> *outp, AddressFamily af, Protocol proto)
{
  Ref<PosixTransport> transport;
  if (Ref<IOError> error = SocketForAddress(&transport, af, proto))
    return error;

  *outp = transport;
  return nullptr;
}

Ref<IOError> AMIO_LINK
amio::net::CreateSocket(Ref<Transport> *outp, Ref<Address> address, Protocol proto)
{
  Ref<PosixTransport> transport;
  if (Ref<IOError> error = SocketForAddress(&transport, address->Family(), proto))
    return error;
  if (Ref<IOError> error = BindTo(transport, address))
    return error;

  *outp = transport;
  return nullptr;
}

Ref<IOError> AMIO_LINK
amio::net::ConnectTo(Ref<Connection> *outp, Protocol protocol, Ref<Address> address)
{
  Ref<PosixConnection> conn;
  if (Ref<IOError> error = ConnectionForAddress(&conn, address, protocol))
    return error;

  int rv = connect(conn->fd(), address->SockAddr(), address->SockAddrLen());
  if (rv == -1)
    return new PosixError();

  // Make the connection non-blocking now that we've connected.
  if (Ref<IOError> error = conn->Setup())
    return error;

  *outp = conn;
  return nullptr;
}

class PosixServer
 : public Server,
   public StatusListener,
   public ke::RefcountedThreadsafe<PosixServer>
{
 public:
  PosixServer(Ref<PosixTransport> transport, Ref<Server::Listener> listener, Ref<Address> address)
   : transport_(transport),
     listener_(listener),
     address_(address),
     closing_(false)
  {
  }

  ~PosixServer() {
    Close();
  }

  void AddRef() override {
    ke::RefcountedThreadsafe<PosixServer>::AddRef();
  }
  void Release() override {
    ke::RefcountedThreadsafe<PosixServer>::Release();
  }

  void OnReadReady() override {
    size_t failures = 0;
    while (failures < 10) {
      int rv = accept(transport_->fd(), nullptr, nullptr);
      if (rv == -1) {
        switch (errno) {
#if defined(KE_LINUX)
          // Linux documents these as being similar to EAGAIN.
          case ENETDOWN:
          case EPROTO:
          case ENOPROTOOPT:
          case EHOSTDOWN:
          case ENONET:
          case EHOSTUNREACH:
          case EOPNOTSUPP:
          case ENETUNREACH:
            // Soft error.
            failures++;
            listener_->OnError(new PosixError(errno), Severity::Warning);
            continue;
#endif
          case EBADF:
          case EINVAL:
            Close();
            listener_->OnError(new PosixError(errno), Severity::Fatal);
            return;
          case EMFILE:
          case ENFILE:
          case ENOBUFS:
          case ENOMEM:
            listener_->OnError(new PosixError(errno), Severity::Severe);
            return;
          case EAGAIN:
#if EAGAIN != EWOULDBLOCK
          case EWOULDBLOCK:
#endif
            return;
          default:
            // Any other error, we don't retry.
            listener_->OnError(new PosixError(errno), Severity::Warning);
            return;
        }

        // Should not reach here, but just in case.
        return;
      }

      // Wrap the new conection in a transport.
      Ref<PosixConnection> conn;
      if (Ref<IOError> error = ConnectionForSocket(&conn, rv, address_->Family())) {
        listener_->OnError(error, Severity::Warning);
        return;
      }
      if (Ref<IOError> error = conn->Setup()) {
        listener_->OnError(error, Severity::Warning);
        return;
      }

      // If the user wants more connections, loop back. Otherwise, we return.
      // Since we're level-triggered here we'll accept more connections next
      // poll.
      if (listener_->Accept(conn) == Action::DeferNext)
        return;
    }
  }
  void OnHangup(Ref<IOError> error) override {
    if (closing_)
      return;
    if (!error)
      error = eUnknownHangup;
    listener_->OnError(error, Severity::Fatal);
  }

  PassRef<Address> ListenAddress() override {
    return address_;
  }
  void Close() override {
    if (closing_)
      return;
    transport_->Close();
    closing_ = true;
  }

 private:
  Ref<PosixTransport> transport_;
  Ref<Server::Listener> listener_;
  Ref<Address> address_;
  bool closing_;
};

PassRef<IOError>
Server::Create(Ref<Server> *outp,
               Ref<IODispatcher> dispatcher,
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

  Ref<PosixTransport> transport;
  if (Ref<IOError> error = SocketForAddress(&transport, address->Family(), protocol))
    return error;

  // Bind and listen.
  if (Ref<IOError> error = BindTo(transport, address))
    return error;
  if (listen(transport->fd(), backlog) == -1)
    return new PosixError();

  struct sockaddr *buf;
  socklen_t buflen;
  Ref<Address> local = address->NewBuffer(&buf, &buflen);

  if (getsockname(transport->fd(), buf, &buflen) == -1)
    return new PosixError();

  Ref<PosixServer> server = new PosixServer(transport, listener, local);
  if (Ref<IOError> error = dispatcher->Attach(transport, server, Events::Read, EventMode::Level))
    return error;

  *outp = server;
  return nullptr;
}

PassRef<IOError>
net::StartNetworking()
{
  return nullptr;
}
