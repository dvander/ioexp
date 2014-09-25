// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#include "../shared/amio-string.h"
#include <am-utility.h>
#include <stdio.h>

#if defined(_MSC_VER)
# define VA_COPY(to, from) to = from
# define vsnprintf _vsnprintf
#else
# define VA_COPY(to, from) va_copy(to, from)
#endif

char *
amio::FormatStringVa(const char *fmt, va_list ap)
{
  va_list use;
  VA_COPY(use, ap);

  size_t maxlength = 255;
  ke::AutoArray<char> buffer(new char[maxlength]);
  if (!buffer)
    return nullptr;

  for (;;) {
    int r = vsnprintf(buffer, maxlength, fmt, use);

    if (r > -1 && size_t(r) < maxlength) {
      // Right-size.
      size_t len = r;
#if !defined(_MSC_VER)
      // Already right-sized. We skip this on MSVC since it has weird
      // null-termination rules.
      if (len + 1 == maxlength)
        return buffer.take();
#endif

      char *result = new char[r + 1];
      memcpy(result, buffer, r);
      result[r] = '\0';
      return result;
    }

    // Compute a new size.
#if defined(_MSC_VER)
    if (r < 0) {
      if (!ke::IsUintPtrMultiplySafe(maxlength, 2))
        return nullptr;
      maxlength *= 2;
    }
#else
    if (r <= -1) {
      if (!ke::IsUintPtrMultiplySafe(maxlength, 2))
        return nullptr;
    } else {
      if (!ke::IsUintPtrAddSafe(maxlength, 1))
        return nullptr;
      maxlength = size_t(r) + 1;
    }
#endif

    buffer = new char[maxlength];
    if (!buffer)
      return nullptr;

    VA_COPY(use, ap);
  }
}

char *
amio::FormatString(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  char *result = amio::FormatStringVa(fmt, ap);
  va_end(ap);
  return result;
}
