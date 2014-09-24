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

#include <am-refcounting.h>
#include "amio-types.h"

namespace amio {

#if defined(AMIO_IMPORT)
# define AMIO_CLASS KE_CLASS_IMPORT
#elif defined(AMIO_EXPORT)
# define AMIO_CLASS KE_CLASS_EXPORT
#else
# define AMIO_CLASS
#endif

using namespace ke;

// Flags that can be used for some TransportFactory functions.
enum AMIO_CLASS TransportFlags
{
  // Automatically close a transport. This is only relevant when a transport is
  // created from an existing operating system construct (such as a file
  // descriptor).
  kTransportAutoClose = 0x00000001,

  kTransportNoFlags  =  0x00000000,
  kTransportDefault  =  kTransportAutoClose
};

class AMIO_CLASS TransportFactory
{
 public:
#if !defined(WIN32)
  // Create a transport from a pre-existing file descriptor.
  static Ref<Transport> CreateFromDescriptor(int fd, TransportFlags flags = kTransportDefault);
#endif
};

// A message pump is responsible for receiving messages. It is not thread-safe.
class AMIO_CLASS MessagePump
{
 public:
   virtual ~MessagePump()
   {}

   // Poll for new events. If |timeoutMs| is non-zero, Poll() may block for
   // at most that many milliseconds. If the message pump has no transports
   // registered, Poll() will exit immediately without an error.
   //
   // An error is returned if the poll itself failed; individual read/write
   // failures are propagated through status listeners.
   //
   // Poll() is not re-entrant.
   virtual Ref<IOError> Poll(uint32_t timeoutMs = 0) = 0;

   // Interrupt a poll operation. The active poll operation will return an error.
   virtual void Interrupt() = 0;

   // Registers a transport with the pump. A transport can be registered to at
   // most one pump at any given time.
   virtual Ref<IOError> Register(Ref<Transport> transport, Ref<StatusListener> listener) = 0;

   // Deregisters a transport from a pump. This happens automatically if the
   // transport is closed.
   virtual void Deregister(Ref<Transport> transport) = 0;
};

// Creates message pumps.
class AMIO_CLASS MessagePumpFactory
{
  // Create a message pump using the best available polling technique. The pump
  // should be freed with |delete| or immediately stored in a ke::AutoPtr.
  static MessagePump *CreatePump();

#if !defined(WIN32)
  // Create a message pump based on select(). Although Windows supports select(),
  // AMIO uses IO Completion Ports which supports much more of the Windows API.
  // For now, select() pumps are not available on Windows.
  static MessagePump *CreateSelectPump();
#endif

#if defined(__linux__)
  // Create a message pump based on poll().
  static MessagePump *CreatePollPump();

  // Create a message pump based on epoll().
  static MessagePump *CreateEpollPump();
#elif defined(__APPLE__) || defined(BSD) || defined(__MACH__)
  // Create a message pump based on kqueue().
  static MessagePump *CreateKqueuePump();
#endif
};

} // namespace amio

#endif // _include_amio_header_h_
