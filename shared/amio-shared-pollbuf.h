// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2014 David Anderson
// 
// This file is part of the AlliedModders I/O Library.
// 
// The AlliedModders I/O library is licensed under the GNU General Public
// License, version 3 or higher. For more information, see LICENSE.txt
//
#ifndef _include_amio_shared_pollbuf_h_
#define _include_amio_shared_pollbuf_h_

#include <am-fixedarray.h>
#include <am-utility.h>
#include <am-thread-utils.h>
#include <limits.h>

namespace amio {

using namespace ke;

template <typename T>
class PollBuffer
{
 public:
  PollBuffer()
   : maxlength_(0),
     absolute_maxlength_(0)
  {}

  bool init(size_t maxLength, size_t absoluteMaxlength = 0) {
    absolute_maxlength_ = absoluteMaxlength;
    if (absolute_maxlength_)
      maxlength_ = absolute_maxlength_;
    else
      maxlength_ = maxLength;
    buffer_ = new T[maxlength_];
    return !!buffer_;
  }

  void maybeResize() {
    if (maxlength_ >= (INT_MAX / 2))
      return;

    size_t newlength = maxlength_ + (maxlength_ / 2);
    if (absolute_maxlength_)
      newlength = ke::Max(absolute_maxlength_, newlength);
    if (newlength == maxlength_)
      return;

    AutoPtr<T> newbuffer(new T[newlength]);
    if (!newbuffer)
      return;

    buffer_ = newbuffer.take();
    maxlength_ = newlength;
  }

  T *get() const {
    return buffer_;
  }
  size_t length() const {
    return maxlength_;
  }
  T &operator [](size_t index) {
    assert(index < maxlength_);
    return buffer_[index];
  }

 private:
  AutoPtr<T> buffer_;
  size_t maxlength_;
  size_t absolute_maxlength_;
};

template <typename T>
class MultiPollBuffer
{
};

}

#endif // _include_amio_shared_pollbuf_h_
