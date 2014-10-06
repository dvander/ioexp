// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_net_h_
#define _include_amio_net_h_

#include <amio.h>
#include <am-platform.h>
#include <am-string.h>
#include <limits.h>
#if defined(KE_POSIX)
# include <string.h>
# include <sys/socket.h>
# include <netinet/in.h>
# if defined(KE_LINUX)
#  include <linux/un.h>
# elif defined(KE_SOLARIS)
#  include <sys/un.h>
# endif
#endif

namespace amio {
namespace net {

using namespace amio;

// Networks types.
enum class AddressFamily
{
  Unknown, // Unknown address family.
  IPv4,    // IPv4 address.
  IPv6,    // IPv6 address.
  Unix     // Unix domain sockets (not available on Windows).
};

// Supported protocols.
enum class Protocol
{
  TCP,
  UDP
};

// Abstract representation of a network address.
class Address : public ke::Refcounted<Address>
{
 public:
  virtual ~Address()
  {}

  // Resolve an address. AMIO cannot guarantee non-blocking resolution ability,
  // so take care: Resolve() can block.
  //
  // IP address strings may end with a ":port" string, where port can be a
  // service name or a numerical port. If |af| is Unknown, then resolution can
  // pick any available address family.
  //
  // IPv6 addresses, if specifying a port or service, should be encased in
  // braces. I.e. [0:0:0:0::0]:80.
  //
  // Returns nullptr with no error if the address could not be resolved.
  static PassRef<Address> Resolve(Ref<IOError> *error, AddressFamily af, const char *address);

  // Return the address family.
  virtual AddressFamily Family() = 0;

  // Format the address as a string. Returns the number of bytes written,
  // not including the null terminator.
  virtual ke::AString ToString() = 0;

  // Return a sockaddr representing the address.
  virtual const struct sockaddr *SockAddr() = 0;

  // Returns the length of the sockaddr.
  virtual size_t SockAddrLen() = 0;
};

// An IPv4 address.
class IPv4Address : public Address
{
 public:
  // Resolve an IPv4 address. AMIO cannot guarantee non-blocking resolution
  // ability, so take care: Resolve() can block.
  //
  // Returns nullptr with no error if the address could not be resolved.
  static PassRef<IPv4Address> Resolve(Ref<IOError> *error, const char *address);

  AddressFamily Family() override {
    return AddressFamily::IPv4;
  }
  const struct sockaddr *SockAddr() override {
    return reinterpret_cast<sockaddr *>(&sin_);
  }
  size_t SockAddrLen() override {
    return sizeof(sin_);
  }
  ke::AString ToString() override;

 private:
  struct sockaddr_in sin_;
};

// An IPv6 address.
class IPv6Address : public Address
{
 public:
  // Resolve an IPv6 address. AMIO cannot guarantee non-blocking resolution
  // ability, so take care: Resolve() can block.
  static PassRef<IPv6Address> Resolve(Ref<IOError> *error, const char *address);

  AddressFamily Family() override {
    return AddressFamily::IPv6;
  }
  const struct sockaddr *SockAddr() override {
    return reinterpret_cast<sockaddr *>(&sin_);
  }
  size_t SockAddrLen() override {
    return sizeof(sin_);
  }
  ke::AString ToString() override;

 private:
  struct sockaddr_in6 sin_;
};

#if defined(KE_POSIX)
// A Unix address.
class UnixAddress : public Address
{
 public:
  static PassRef<UnixAddress> Resolve(Ref<IOError> *error, const char *address);

  AddressFamily Family() override {
    return AddressFamily::Unix;
  }
  const struct sockaddr *SockAddr() override {
    return reinterpret_cast<sockaddr *>(&sun_);
  }
  size_t SockAddrLen() override {
    return offsetof(sockaddr_un, sun_family) + strlen(sun_.sun_path);
  }
  ke::AString ToString() override;

 private:
  struct sockaddr_un sun_;
};
#endif

// A net listener accepts network connections on a port.
class Server :
  public IPollable,
  public ke::VirtualRefcountedThreadsafe
{
 public:
  // Events on this listener can be fired upon polling.
  class Listener : public ke::VirtualRefcountedThreadsafe
  {
   public:
    // Called when a new connection is available.
    virtual void Accept(Ref<amio::Transport> *transport, Ref<Address> address)
    {}

    // Called when an error occurs accepting connections.
    virtual void OnError(Ref<IOError> error)
    {}
  };

  // Create a new server on the given address. On success, a non-null server
  // is returned. It can be added to a poller to begin receiving eveonts on
  // the given listener.
  static PassRef<Server> *Create(
    Ref<IOError> *error,
    Ref<Address> address,
    Ref<Listener> listener
  );

  // Return the address the server is listening on.
  virtual Ref<Address> ListenAddress() = 0;

  // Close the server; stops accepting requests.
  virtual void Close() = 0;
};

class Client
 : public IPollable,
   public ke::VirtualRefcountedThreadsafe
{
 public:
  // Events on this listener can be fired upon polling.
  class Listener
  {
   public:
    // Called when the socket is connected.
    virtual void OnConnect(Ref<amio::Transport> transport)
    {}

    // Called when an error was encountered connecting.
    virtual void OnError(Ref<IOError> error)
    {}
  };

  // Create a socket that can be polled for when it has connected to the given
  // address. The client object should be freed with |delete| or stored in a
  // ke::AutoPtr. It can be discarded at any time after being given to a poller,
  // as it is only used to communicate polling parameters. The listener will
  // still be associated as long as the Attach() call succeeded.
  static PassRef<Client> Create(
    Ref<IOError> *error,
    Ref<Address> address,
    Ref<Listener> listener
  );
};

// Creates a connection to an address. This call will block.
AMIO_LINK Ref<Transport> ConnectTo(Protocol protocol, Ref<Address> address);

#if defined(KE_WINDOWS)
// Creates a socket connected to an address. This call will block.
AMIO_LINK Ref<Socket> ConnectSocketTo(Protocol protocol, Ref<Address> address);
#endif

} // net
} // amio

#endif // _include_amio_net_h_
