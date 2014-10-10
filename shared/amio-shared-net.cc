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
#if defined(KE_WINDOWS)
# include "../windows/amio-windows-errors.h"
#else
# include <netdb.h>
# include <arpa/inet.h>
# include "../posix/amio-posix-errors.h"
#endif
#include "../shared/amio-string.h"

using namespace ke;
using namespace amio;
using namespace amio::net;

static Ref<GenericError> sInvalidIPv4Length = new GenericError("ipv4 address has invalid length");
static Ref<GenericError> sInvalidIPv6Length = new GenericError("ipv6 address has invalid length");
static Ref<GenericError> sUnknownResolutionError = new GenericError("unknown error resolving address");

inline PassRef<IOError>
NetworkError()
{
#if defined(KE_WINDOWS)
  return new WinsockError();
#else
  return new PosixError();
#endif
}

#if defined(KE_WINDOWS)
static inline const char *
inet_ntop(int af, const void *src, char *dst, size_t len)
{
  return InetNtopA(af, (void *)src, dst, len);
}

static inline PassRef<IOError>
try_getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
  if (getaddrinfo(node, service, hints, res) != 0)
    return new WinsockError();
  return nullptr;
}
#else
static inline PassRef<IOError>
try_getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
  int rv = getaddrinfo(node, service, hints, res);
  if (rv != 0) {
    const char *str = gai_strerror(rv);
    if (!str)
      return sUnknownResolutionError;
    return new GenericError("%s", str);
  }
  return nullptr;
}
#endif

#define STUB_CAST(Kind)               \
  PassRef<Kind> Address::to##Kind() { \
    assert(as##Kind());               \
    return as##Kind();                \
  }                                   \
  PassRef<Kind> Address::as##Kind() { \
    return nullptr;                   \
  }

STUB_CAST(IPAddress);
STUB_CAST(IPv4Address);
STUB_CAST(IPv6Address);
STUB_CAST(UnixAddress);

PassRef<Address>
Address::Copy()
{
  struct sockaddr *buffer;
  socklen_t buflen;
  Ref<Address> addr = this->NewBuffer(&buffer, &buflen);

  assert(buflen < addr->SockAddrLen());
  memcpy(buffer, addr->SockAddr(), ke::Min(addr->SockAddrLen(), buflen));
  return addr;
}

IPv4Address::IPv4Address()
{
}

IPv4Address::IPv4Address(struct sockaddr **buf, socklen_t *buflen)
{
  *buf = reinterpret_cast<struct sockaddr *>(&buf_);
  *buflen = sizeof(buf_);
}

PassRef<IPv4Address>
IPv4Address::Resolve(Ref<IOError> *error, const char *address)
{
  AString temp;
  const char *service = nullptr;
  if (const char *ptr = strchr(address, ':')) {
    AString cut(address, size_t(ptr - address));
    temp = Move(cut);
    address = temp.chars();
    if (strcmp(ptr, ":0") != 0)
      service = ptr + 1;
  }

#if defined(KE_SOLARIS)
  // Workaround a bug on Solaris.
  int port = service ? atoi(service) : 0;
  service = nullptr;
#endif

  struct addrinfo hint;
  memset(&hint, 0, sizeof(hint));
  hint.ai_family = AF_INET;

  struct addrinfo *info;
  *error = try_getaddrinfo(address, service, &hint, &info);
  if (*error)
    return nullptr;

  if (info->ai_addrlen != sizeof(IPv4Address::buf_)) {
    freeaddrinfo(info);
    *error = sInvalidIPv4Length;
    return nullptr;
  }

  Ref<IPv4Address> ipv4 = new IPv4Address;
  memcpy(&ipv4->buf_, info->ai_addr, sizeof(IPv4Address::buf_));
  freeaddrinfo(info);

#if defined(KE_SOLARIS)
  if (port)
    ipv4->buf_.sin_port = htons(port);
#endif
  return ipv4;
}

AString
IPv4Address::ToString()
{
  char tmp[255];
  if (!inet_ntop(AF_INET, &buf_.sin_addr, tmp, sizeof(tmp)))
    return AString("unknown");

  char buffer[255];
  if (buf_.sin_port) {
    FormatArgs(buffer, sizeof(buffer), "%s:%d", tmp, ntohs(buf_.sin_port));
    return AString(buffer);
  }
  return AString(tmp);
}

PassRef<Address>
IPv4Address::NewBuffer(sockaddr **outp, socklen_t *lenp)
{
  Ref<IPv4Address> addr = new IPv4Address();
  *outp = reinterpret_cast<sockaddr *>(&addr->buf_);
  *lenp = sizeof(addr->buf_);
  return addr;
}

IPv6Address::IPv6Address()
{
}

IPv6Address::IPv6Address(struct sockaddr **buf, socklen_t *buflen)
{
  *buf = reinterpret_cast<struct sockaddr *>(&buf_);
  *buflen = sizeof(buf_);
}

PassRef<IPv6Address>
IPv6Address::Resolve(Ref<IOError> *error, const char *address)
{
  AString temp;
  const char *service = nullptr;

  // Cut off [] - we do this even if there isn't a port.
  if (const char *ptr = strchr(address, ']')) {
    if (*address == '[') {
      AString cut(address + 1, size_t(ptr - address - 1));
      temp = Move(cut);
      address = temp.chars();

      if (const char *cp = strchr(ptr, ':')) {
        if (strcmp(cp, ":0") != 0)
          service = cp + 1;
      }
    }
  }

  Ref<IPv6Address> ipv6 = new IPv6Address;
  memset(&ipv6->buf_, 0, sizeof(ipv6->buf_));

#if defined(KE_SOLARIS)
  // Workaround a bug on Solaris.
  int port = service ? atoi(service) : 0;
  service = nullptr;
#endif

  struct addrinfo hint;
  memset(&hint, 0, sizeof(hint));
  hint.ai_family = AF_INET6;

  struct addrinfo *info;
  *error = try_getaddrinfo(address, service, &hint, &info);
  if (*error)
    return nullptr;

  if (info->ai_addrlen != sizeof(IPv6Address::buf_)) {
    freeaddrinfo(info);
    *error = sInvalidIPv6Length;
    return nullptr;
  }

  memcpy(&ipv6->buf_, info->ai_addr, sizeof(IPv6Address::buf_));
  freeaddrinfo(info);

#if defined(KE_SOLARIS)
  if (port)
    ipv6->buf_.sin6_port = htons(port);
#endif
  return ipv6;
}

AString
IPv6Address::ToString()
{
  char tmp[255];
  if (!inet_ntop(AF_INET6, &buf_.sin6_addr, tmp, sizeof(tmp)))
    return AString("unknown");

  char buffer[255];
  if (buf_.sin6_port) {
    FormatArgs(buffer, sizeof(buffer), "[%s]:%d", tmp, ntohs(buf_.sin6_port));
    return AString(buffer);
  }
  return AString(tmp);
}

PassRef<Address>
IPv6Address::NewBuffer(sockaddr **outp, socklen_t *lenp)
{
  Ref<IPv6Address> addr = new IPv6Address();
  *outp = reinterpret_cast<sockaddr *>(&addr->buf_);
  *lenp = sizeof(addr->buf_);
  return addr;
}
