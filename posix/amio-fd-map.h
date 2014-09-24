// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_fd_map_h_
#define _include_amio_fd_map_h_

#include <am-hashmap.h>

namespace amio {

template <typename T>
class DescriptorMap
{
 public:
  void add(int fd, const T& t);
  T *find(int fd);
  void remove(int fd);
  size_t length();
};

} // namespace amio

#endif // _include_amio_fd_map_h_
