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
#include <am-vector.h>
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
  T &at(size_t index) {
    assert(index < maxlength_);
    return buffer_[index];
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
 public:
  MultiPollBuffer() {
    lock_ = new Mutex();
  }
  ~MultiPollBuffer() {
    for (size_t i = 0; i < buffers_.length(); i++)
      delete buffers_[i];
  }

  bool init(size_t maxLength, size_t absoluteMaxlength = 0) {
    AutoPtr<PollBuffer<T>> buffer(new PollBuffer<T>());
    if (!buffer->init(maxLength, absoluteMaxlength))
      return false;

    if (!buffers_.append(buffer))
      return false;
    buffer.forget();

    maxlength_ = buffers_[0]->length();
    absolute_maxlength_ = absoluteMaxlength;
    return true;
  }

  PollBuffer<T> *get() {
    AutoLock lock(lock_);
    if (!buffers_.empty())
      return buffers_.popCopy();

    AutoPtr<PollBuffer<T>> buffer(new PollBuffer<T>());
    if (!buffer->init(maxlength_, absolute_maxlength_))
      return nullptr;

    return buffer.take();
  }
  void put(PollBuffer<T> *aBuffer) {
    AutoPtr<PollBuffer<T>> buffer(aBuffer);

    AutoLock lock(lock_);
    if (buffers_.append(buffer)) {
      buffer.forget();
      return;
    }
  }

 public:
  class Use
  {
   public:
    Use(MultiPollBuffer<T> &parent)
     : parent_(parent),
       buffer_(parent.get())
    {}
    ~Use() {
      if (buffer_)
        parent_.put(buffer_);
    }

    PollBuffer<T> *get() const {
      return buffer_;
    }

   private:
    MultiPollBuffer<T> &parent_;
    PollBuffer<T> *buffer_;
  };

 private:
  AutoPtr<Mutex> lock_;
  Vector<PollBuffer<T> *> buffers_;
  size_t maxlength_;
  size_t absolute_maxlength_;
};

}

#endif // _include_amio_shared_pollbuf_h_
