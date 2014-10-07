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

PassRef<UnixAddress>
UnixAddress::Resolve(Ref<IOError> *error, const char *address)
{
  if (strlen(address) > kMaxUnixPath) {
    *error = sUnixNameTooLong;
    return nullptr;
  }

  Ref<UnixAddress> out = new UnixAddress;
  out->sun_.sun_family = AF_UNIX;
  CopyString(out->sun_.sun_path, sizeof(out->sun_.sun_path), address);
#if defined(SOCKADDR_LEN)
  out->sun_.sun_len = SUN_LEN(&out->sun_);
#endif

  return out;
}

size_t
UnixAddress::SockAddrLen()
{
  return SUN_LEN(&sun_);
}

AString
UnixAddress::ToString()
{
  return AString(sun_.sun_path);
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

class PosixClient
  : public virtual Client,
    public virtual StatusListener
{
 public:
  PosixClient(Ref<PosixTransport> transport, Ref<Address> address, Ref<Client::Listener> listener)
   : transport_(transport),
     address_(address),
     listener_(listener),
     connected_(false)
  {
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

  PassRef<Transport> GetTransport() override {
    return transport_;
  }
  bool Connected() override {
    return connected_;
  }

 private:
  Ref<PosixTransport> transport_;
  Ref<Address> address_;
  Ref<Client::Listener> listener_;
  bool connected_;
};

PassRef<Client>
Client::Create(Ref<IOError> *error,
               Ref<Address> address, Protocol protocol,
               Ref<Client::Listener> listener)
{
  int fd;
  if ((*error = SocketForAddress(&fd, address, protocol)))
    return nullptr;

  Ref<PosixTransport> transport = new PosixTransport(fd, kTransportDefaultFlags);
  if ((*error = transport->Setup()) != nullptr)
    return nullptr;
  return new PosixClient(transport, address, listener);
}

Ref<Transport> AMIO_LINK
amio::net::ConnectTo(Ref<IOError> *error, Protocol protocol, Ref<Address> address)
{
  int fd;
  if ((*error = SocketForAddress(&fd, address, protocol)))
    return nullptr;

  int rv = connect(fd, address->SockAddr(), address->SockAddrLen());
  if (rv == -1) {
    *error = new PosixError();
    close(fd);
    return nullptr;
  }

  Ref<PosixTransport> transport = new PosixTransport(fd, kTransportDefaultFlags);
  if ((*error = transport->Setup()) != nullptr)
    return nullptr;
  return transport;
}

class PosixServer
 : public Server,
   public StatusListener
{
 public:
  PosixServer(Ref<PosixTransport> transport, Ref<Server::Listener> listener, Ref<Address> address)
   : transport_(transport),
     listener_(listener),
     address_(address)
  {
  }

  PassRef<IOError> Attach(Poller *poller) override {
  }

  PassRef<Address> ListenAddress() override {
    return address_;
  }
  void Close() override {
    transport_->Close();
  }

 private:
  Ref<PosixTransport> transport_;
  Ref<Server::Listener> listener_;
  Ref<Address> address_;
};

PassRef<Server>
Server::Create(Ref<IOError> *error,
               Ref<Address> address, Protocol protocol,
               Ref<Server::Listener> listener,
               unsigned backlog)
{
  switch (protocol) {
    case Protocol::TCP:
    case Protocol::Stream:
      break;
    default:
      *error = eUnsupportedProtocol;
      return nullptr;
  }
  if (!backlog)
    backlog = SOMAXCONN;

  int fd;
  if ((*error = SocketForAddress(&fd, address, protocol)))
    return nullptr;

  Ref<PosixTransport> transport = new PosixTransport(fd, kTransportDefaultFlags);
  if ((*error = transport->Setup()) != nullptr)
    return nullptr;

  if (bind(transport->fd(), address->SockAddr(), address->SockAddrLen()) == -1) {
    *error = new PosixError();
    return nullptr;
  }
  if (listen(transport->fd(), backlog) == -1) {
    *error = new PosixError();
    return nullptr;
  }

  return new PosixServer(transport, listener, address);
}
