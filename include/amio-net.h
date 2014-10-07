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
# include <sys/un.h>
#endif

namespace amio {
namespace net {

using namespace amio;

// Networks types.
enum class AddressFamily
{
  IPv4,     // IPv4 address.
  IPv6,     // IPv6 address.
  Unix,     // Unix domain sockets (not available on Windows).
  Unknown   // Unknown address family.
};

// Supported protocols.
enum class Protocol
{
  TCP,      // Streaming for IPv4 or IPv6.
  UDP,      // Datagrams for IPv4 or IPv6.
  Stream,   // Any stream protocol available. With AF::IP, this is TCP.
            // With AF::Unix, it is a byte stream protocol.
  Datagram, // Any datagram protocol available. With AF::IP, this is TCP.
            // With AF::Unix, it is a message protocol.
  Unknown
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
  size_t SockAddrLen() override;
  ke::AString ToString() override;

 private:
  struct sockaddr_un sun_;
};
#endif

// A server accepts network connections on a connection-oriented port.
class Server :
  public IPollable,
  public ke::VirtualRefcountedThreadsafe
{
 public:
  enum class Action {
    // Instructs the I/O layer to try and consume another connection, as long
    // as it would not block to do so. Note that, like all edge-triggered APIs,
    // risks starving other I/O operations on the same thread.
    Again,

    // Instructs the I/O layer to defer any pending connection notifications
    // until the next call to Poll().
    DeferNext
  };

  // Events on this listener can be fired upon polling.
  class Listener : public ke::VirtualRefcountedThreadsafe
  {
   public:
    // Called when a new connection is available.
    virtual Action Accept(Ref<amio::Transport> *transport, Ref<Address> address) {
      return Action::DeferNext;
    }

    // Called when an error occurs accepting connections.
    virtual void OnError(Ref<IOError> error)
    {}

    // Called on a fatal error - such as ENOMEM (out of memory) or EMFILE,
    // which indicates there are no more socket descriptors available.
    //
    // Nothing happens on a fatal error, it is just a hint for the application
    // that the error may happen repeatedly and may require shutdown.
    virtual void OnFatalError(Ref<IOError> error)
    {}
  };

  // Create a new server on the given address. On success, a non-null server
  // is returned. It can be added to a poller to begin receiving events on
  // the given listener.
  //
  // Backlog specifies the maximum number of pending connections that can be
  // enqueued. Use 0 for the default (usually 128).
  static PassRef<Server> Create(
    Ref<IOError> *error,
    Ref<Address> address,
    Protocol protocol,
    Ref<Server::Listener> listener,
    unsigned backlog = 0
  );

  // Return the address the server is listening on.
  virtual PassRef<Address> ListenAddress() = 0;

  // Close the server; stops accepting requests, and terminates any outstanding
  // connections.
  virtual void Close() = 0;
};

class Client
 : public IPollable,
   public ke::VirtualRefcountedThreadsafe
{
 public:
  // Events on this listener can be fired upon polling.
  class Listener : public Poller::Listener
  {
   public:
    // Called when the socket is connected.
    virtual void OnConnect(Ref<amio::Transport> transport)
    {}
  };

  // Create a socket that can be polled for when it has connected to the given
  // address. After attaching to a poller, use Connected() to determine if the
  // socket has immediately connected. If it has, then OnConnect() will not be
  // called. If it has not, then OnConnect or OnError will be called during a
  // Poll() operation to indicate success or failure. If failure is indicated,
  // the underlying transport is closed after notification.
  //
  // Client::Create() should be used for any type of client-oriented transport,
  // even connection-less protocols.
  //
  // It is not necessary to hold onto Client objects; they are only used to
  // communicate initial connection state.
  //
  // Parameters:
  //  error:     If Create() returns null, this will be set to an error object.
  //  address:   The address to connect to upon attaching.
  //  protocol:  The protocol to use for the socket.
  //  listener:  Listener to receive event callbacks.
  static PassRef<Client> Create(
    Ref<IOError> *error,
    Ref<Address> address,
    Protocol protocol,
    Ref<Client::Listener> listener
  );

  // Returns the underlying transport being used by the connection operation.
  // This is guaranteed to be available immediately (i.e. it is not delayed
  // until attaching). However, it is immediately closed if the connection
  // fails (pending notifications if the error was asynchronous).
  virtual PassRef<Transport> GetTransport() = 0;

  // Returns true if the socket has connected; false otherwise. No connection
  // attempt is made until the client has been attached to a poller.
  virtual bool Connected() = 0;
};

// Creates a connection to an address. This call will block while making the
// connection (if the protocol is connection-oriented). Afterward, the
// transport is in non-blocking mode so it can be used with Pollers.
AMIO_LINK Ref<Transport> ConnectTo(Ref<IOError> *error, Protocol protocol, Ref<Address> address);

#if defined(KE_WINDOWS)
// Creates a socket connected to an address. This call will block.
AMIO_LINK Ref<Socket> ConnectSocketTo(Protocol protocol, Ref<Address> address);
#endif

} // net
} // amio

#endif // _include_amio_net_h_
