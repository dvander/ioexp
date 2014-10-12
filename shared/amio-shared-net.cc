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

static Ref<IPv4Address> sNullIPv4Address;
static Ref<IPv6Address> sNullIPv6Address;

class InitNetworkVars
{
 public:
  InitNetworkVars() {
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sNullIPv4Address = new IPv4Address(sin);
  
    struct sockaddr_in6 sin6;
    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    sNullIPv6Address = new IPv6Address(sin6);
  }
} sInitNetworkVars;

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
  int rv = getaddrinfo(node, service, hints, res);
  if (rv != 0)
    return new WinError(rv);
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

#define STUB_TO_CAST(Kind)            \
  PassRef<Kind> Address::to##Kind() { \
    assert(as##Kind());               \
    return as##Kind();                \
  }
#define STUB_AS_CAST(Kind)            \
  PassRef<Kind> Address::as##Kind() { \
    return nullptr;                   \
  }
#define STUB_CAST(Kind)               \
  STUB_TO_CAST(Kind)                  \
  STUB_AS_CAST(Kind)

STUB_CAST(IPAddress);
STUB_CAST(IPv4Address);
STUB_CAST(IPv6Address);

#if defined(KE_POSIX)
STUB_CAST(UnixAddress);
#endif

PassRef<IOError>
Address::AnyAddress(Ref<Address> *outp, AddressFamily af)
{
  switch (af) {
    case AddressFamily::IPv4:
      *outp = sNullIPv4Address;
      return nullptr;
    case AddressFamily::IPv6:
      *outp = sNullIPv6Address;
      return nullptr;
    default:
      return eUnsupportedAddressFamily;
  }
}

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

IPv4Address::IPv4Address(const struct sockaddr_in &buf)
{
  buf_ = buf;
}

IPv4Address::IPv4Address(struct sockaddr **buf, socklen_t *buflen)
{
  *buf = reinterpret_cast<struct sockaddr *>(&buf_);
  *buflen = sizeof(buf_);
}

PassRef<IOError>
IPv4Address::Resolve(Ref<IPv4Address> *outp, const char *address)
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
  if (Ref<IOError> error = try_getaddrinfo(address, service, &hint, &info))
    return error;

  if (info->ai_addrlen != sizeof(sockaddr_in)) {
    freeaddrinfo(info);
    return sInvalidIPv4Length;
  }

  Ref<IPv4Address> ipv4 = new IPv4Address(*(sockaddr_in *)info->ai_addr);
  freeaddrinfo(info);

#if defined(KE_SOLARIS)
  if (port)
    ipv4->buf_.sin_port = htons(port);
#endif
  *outp = ipv4;
  return nullptr;
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

IPv6Address::IPv6Address(const struct sockaddr_in6 &buf)
{
  buf_ = buf;
}

PassRef<IOError>
IPv6Address::AnyAddress(Ref<IPv6Address> *outp)
{
  *outp = sNullIPv6Address;
  return nullptr;
}

PassRef<IOError>
IPv6Address::Resolve(Ref<IPv6Address> *outp, const char *address)
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

#if defined(KE_SOLARIS)
  // Workaround a bug on Solaris.
  int port = service ? atoi(service) : 0;
  service = nullptr;
#endif

  struct addrinfo hint;
  memset(&hint, 0, sizeof(hint));
  hint.ai_family = AF_INET6;

  struct addrinfo *info;
  if (Ref<IOError> error = try_getaddrinfo(address, service, &hint, &info))
    return error;

  if (info->ai_addrlen != sizeof(sockaddr_in6)) {
    freeaddrinfo(info);
    return sInvalidIPv6Length;
  }

  Ref<IPv6Address> ipv6 = new IPv6Address(*(struct sockaddr_in6 *)info->ai_addr);
  freeaddrinfo(info);

#if defined(KE_SOLARIS)
  if (port)
    ipv6->buf_.sin6_port = htons(port);
#endif

  *outp = ipv6;
  return nullptr;
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
