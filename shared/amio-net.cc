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

static inline PassRef<IOError>
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
#endif

PassRef<IPv4Address>
IPv4Address::Resolve(Ref<IOError> *error, const char *address)
{
  AString temp;
  const char *service = nullptr;
  if (const char *ptr = strchr(address, ':')) {
    AString cut(address, size_t(ptr- address));
    temp = Move(cut);
    address = temp.chars();
    if (strcmp(ptr, ":0") != 0)
      service = ptr + 1;
  }

  struct addrinfo hint;
  memset(&hint, 0, sizeof(hint));
  hint.ai_family = AF_INET;

  struct addrinfo *info;
  if (getaddrinfo(address, service, &hint, &info) == -1) {
    *error = NetworkError();
    return nullptr;
  }

  if (info->ai_addrlen != sizeof(IPv4Address::sin_)) {
    freeaddrinfo(info);
    *error = sInvalidIPv4Length;
    return nullptr;
  }

  Ref<IPv4Address> ipv4 = new IPv4Address;
  memcpy(&ipv4->sin_, info->ai_addr, sizeof(IPv4Address::sin_));
  freeaddrinfo(info);
  return ipv4;
}

AString
IPv4Address::ToString()
{
  char tmp[255];
  if (!inet_ntop(AF_INET, &sin_.sin_addr, tmp, sizeof(tmp)))
    return AString("unknown");

  char buffer[255];
  if (sin_.sin_port) {
    FormatArgs(buffer, sizeof(buffer), "%s:%d", tmp, ntohs(sin_.sin_port));
    return AString(buffer);
  }
  return AString(tmp);
}

PassRef<IPv6Address>
IPv6Address::Resolve(Ref<IOError> *error, const char *address)
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

  // Cut off [] - we do this even if there wasn't a port.
  if (const char *ptr = strchr(address, ']')) {
    if (*address == '[' && *(ptr + 1) == '\0') {
      AString cut(address + 1, size_t(ptr - address));
      temp = Move(cut);
      address = temp.chars();
    }
  }

  struct addrinfo hint;
  memset(&hint, 0, sizeof(hint));
  hint.ai_family = AF_INET6;

  struct addrinfo *info;
  if (getaddrinfo(address, service, &hint, &info) == -1) {
    *error = NetworkError();
    return nullptr;
  }

  if (info->ai_addrlen != sizeof(IPv6Address::sin_)) {
    freeaddrinfo(info);
    *error = sInvalidIPv6Length;
    return nullptr;
  }

  Ref<IPv6Address> ipv6 = new IPv6Address;
  memcpy(&ipv6->sin_, info->ai_addr, sizeof(IPv6Address::sin_));
  freeaddrinfo(info);
  return ipv6;
}

AString
IPv6Address::ToString()
{
  char tmp[255];
  if (!inet_ntop(AF_INET6, &sin_.sin6_addr, tmp, sizeof(tmp)))
    return AString("unknown");

  char buffer[255];
  if (sin_.sin6_port) {
    FormatArgs(buffer, sizeof(buffer), "[%s]:%d", tmp, ntohs(sin_.sin6_port));
    return AString(buffer);
  }
  return AString(tmp);
}
