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

PassRef<UnixAddress>
UnixAddress::Resolve(Ref<IOError> *error, const char *address)
{
  if (strlen(address) > kMaxUnixPath) {
    *error = sUnixNameTooLong;
    return nullptr;
  }

  Ref<UnixAddress> out = new UnixAddress;
  out->buf_.sun_family = AF_UNIX;
  CopyString(out->buf_.sun_path, sizeof(out->buf_.sun_path), address);
#if defined(SOCKADDR_LEN)
  out->buf_.sun_len = SUN_LEN(&out->buf_);
#endif

  return out;
}

size_t
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
SocketForAddress(int *outp, Ref<Address> address, Protocol protocol)
{
  *outp = -1;

  int af;
  switch (address->Family()) {
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
  PosixConnectionT(int fd, TransportFlags flags)
   : PosixConnection(fd, flags)
  {}

  PassRef<IOError> RemoteAddress(Ref<Address> *outp) override {
    struct sockaddr *buf;
    socklen_t buflen;
    Ref<T> addr = new T(&buf, &buflen);
    if (getsockname(this->fd(), buf, &buflen) == -1)
      return new PosixError();
    *outp = addr;
    return nullptr;
  }

  PassRef<IOError> LocalAddress(Ref<Address> *outp) override {
    struct sockaddr *buf;
    socklen_t buflen;
    Ref<T> addr = new T(&buf, &buflen);
    if (getpeername(this->fd(), buf, &buflen) == -1)
      return new PosixError();
    *outp = addr;
    return nullptr;
  }
};

#if 0
class PosixClient
 : public StatusListener,
   public ke::Refcounted<PosixClient>
{
 public:
  PosixClient(Ref<PosixTransport> transport, Ref<Address> address, Ref<Client::Listener> listener)
   : transport_(transport),
     address_(address),
     listener_(listener),
     connected_(false)
  {
  }

  void AddRef() override {
    ke::Refcounted<PosixClient>::AddRef();
  }
  void Release() override {
    ke::Refcounted<PosixClient>::Release();
  }

  PassRef<IOError> Attach(Poller *poller) {
    Ref<IOError> error;
    int rv = connect(transport_->fd(), address_->SockAddr(), address_->SockAddrLen());
    if (rv == 0) {
      if ((error = poller->Attach(transport_, listener_, Events_None)) == nullptr)
        return nullptr;
    } else {
      if (errno != EINPROGRESS)
        error = new PosixError();
    }

    // If we got no errors, but no connection, hijack the write event to find
    // when connect() finishes.
    if (!error)
      error = poller->Attach(transport_, this, Event_Write);

    if (error) {
      // On error we immediately close the descriptor.
      transport_->Close();
      return error;
    }
    return nullptr;
  }

  void OnWriteReady(Ref<Transport> transport) override {
    assert(transport == transport_);

    int errn;
    socklen_t len = sizeof(errn);
    int rv = getsockopt(transport->FileDescriptor(), SOL_SOCKET, SO_ERROR, &errn, &len);
    if (rv == -1 || errn != 0) {
      Ref<IOError> error = new PosixError(rv == -1 ? errno : errn);
      listener_->OnError(transport, error);
      transport->Close();
      return;
    }

    transport_->changeListener(listener_);
    listener_->OnConnect(transport_);
  }

 private:
  Ref<PosixTransport> transport_;
  Ref<Address> address_;
  Ref<Client::Listener> listener_;
};
#endif

static inline PassRef<IOError>
ConnectionForAddress(Ref<PosixConnection> *outp, Ref<Address> address, Protocol protocol)
{
  int fd;
  Ref<IOError> error = SocketForAddress(&fd, address, protocol);
  if (error)
    return error;

  switch (address->Family()) {
    case AddressFamily::IPv4:
      *outp = new PosixConnectionT<IPv4Address>(fd, kTransportDefaultFlags);
      return nullptr;
    case AddressFamily::IPv6:
      *outp = new PosixConnectionT<IPv6Address>(fd, kTransportDefaultFlags);
      return nullptr;
    case AddressFamily::Unix:
      *outp = new PosixConnectionT<UnixAddress>(fd, kTransportDefaultFlags);
      return nullptr;
    default:
      return eUnsupportedAddressFamily;
  }
}

bool
Client::Create(Result *result, Ref<Poller> poller,
               Ref<Address> address, Protocol protocol,
               Ref<Client::Listener> listener, EventFlags events)
{
  *result = Result();

  Ref<PosixConnection> conn;
  if (Ref<IOError> error = ConnectionForAddress(&conn, address, protocol)) {
    result->error = error;
    return false;
  }
  if (Ref<IOError> error = conn->Setup()) {
    result->error = error;
    return false;
  }

  int rv = connect(conn->fd(), address->SockAddr(), address->SockAddrLen());
  if (rv == 0) {
    result->connection = conn;
    return true;
  }

  //Ref<ConnectionListener> listener = new ConnectionListener(
  return true;
}

Ref<IOError> AMIO_LINK
amio::net::ConnectTo(Ref<Connection> *outp, Protocol protocol, Ref<Address> address)
{
  Ref<PosixConnection> conn;
  Ref<IOError> error = ConnectionForAddress(&conn, address, protocol);
  if (error)
    return error;

  int rv = connect(conn->fd(), address->SockAddr(), address->SockAddrLen());
  if (rv == -1)
    return new PosixError();

  // Make the connection non-blocking now that we've connected.
  if ((error = conn->Setup()))
    return error;

  *outp = conn;
  return nullptr;
}

class PosixServer
 : public Server,
   public StatusListener,
   public ke::Refcounted<PosixServer>
{
 public:
  PosixServer(Ref<PosixTransport> transport, Ref<Server::Listener> listener, Ref<Address> address)
   : transport_(transport),
     listener_(listener),
     address_(address),
     closing_(false)
  {
  }

  void AddRef() override {
    ke::Refcounted<PosixServer>::AddRef();
  }
  void Release() override {
    ke::Refcounted<PosixServer>::Release();
  }

  PassRef<IOError> Attach(Poller *poller) override {
    return poller->Attach(transport_, this, Event_Read|Event_Sticky);
  }

  void OnReadReady(Ref<Transport> server) override {
    assert(server == transport_);

    sockaddr *addr;
    socklen_t addrlen, orig_addrlen;
    Ref<Address> remote = address_->NewBuffer(&addr, &orig_addrlen);

    size_t failures = 0;
    while (failures < 10) {
      addrlen = orig_addrlen;
      int rv = accept(transport_->fd(), addr, &addrlen);
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
      Ref<PosixTransport> connection = new PosixTransport(rv, kTransportDefaultFlags);
      Ref<IOError> error = connection->Setup();
      if (error) {
        failures++;
        listener_->OnError(error, Severity::Warning);
        continue;
      }

      // If the user wants more connections, loop back. Otherwise, we return.
      // Since we're level-triggered here we'll accept more connections next
      // poll.
      if (listener_->Accept(connection, remote) == Action::DeferNext)
        return;
    }
  }
  void OnHangup(Ref<Transport> server) override {
    assert(server == transport_);
    if (closing_)
      return;
    listener_->OnError(eUnknownHangup, Severity::Fatal);
  }
  void OnError(Ref<Transport> server, Ref<IOError> error) override {
    assert(server == transport_);
    if (closing_)
      return;
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

  int fd;
  if (Ref<IOError> error = SocketForAddress(&fd, address, protocol))
    return error;

  Ref<PosixTransport> transport = new PosixTransport(fd, kTransportDefaultFlags);
  if (Ref<IOError> error = transport->Setup())
    return nullptr;

  // Before we bind, set SO_REUSEADDR.
  int enable = 1;
  if (setsockopt(transport->fd(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1)
    return new PosixError();

  // Bind and listen.
  if (bind(transport->fd(), address->SockAddr(), address->SockAddrLen()) == -1)
    return new PosixError();
  if (listen(transport->fd(), backlog) == -1)
    return new PosixError();

  struct sockaddr *buf;
  socklen_t buflen;
  Ref<Address> local = address->NewBuffer(&buf, &buflen);

  if (getsockname(transport->fd(), buf, &buflen) == -1)
    return new PosixError();

  *outp = new PosixServer(transport, listener, local);
  return nullptr;
}
