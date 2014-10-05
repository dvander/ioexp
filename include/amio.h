// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_header_h_
#define _include_amio_header_h_

#include <amio-types.h>

#if defined(_WIN32)
# include <amio-windows.h>
#else
# include <amio-posix.h>
#endif

#endif // _include_amio_header_h_
