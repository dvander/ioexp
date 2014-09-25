// vim: set ts=8 sw=2 sts=2 tw=99 et:
#include <stdio.h>
#include <stdlib.h>
#include <amio.h>
//#include "posix/amio-posix-poll.h"
#include "linux/amio-linux-epoll.h"

using namespace ke;
using namespace amio;

class InStatus : public StatusListener
{
  void OnReadReady(Ref<Transport> transport) override
  {
    for (;;) {
      IOResult result;
      uint8_t buffer[255];
      if (!transport->Read(&result, buffer, sizeof(buffer)))
        abort();
      printf("got bytes: %ld\n", result.Bytes);
      if (result.Ended)
        abort();
      if (!result.Bytes)
        break;
      for (size_t i = 0; i < result.Bytes; i++)
        printf("%c", buffer[i]);
    }
  }
  void OnWriteReady(Ref<Transport> transport) override
  {
  }
  void OnHangup(Ref<Transport> transport) override
  {
  }
  void OnError(Ref<Transport> transport, Ref<IOError> error) override
  {
  }
};

int main()
{
  EpollMessagePump pump;
  MaybeTransport mt = TransportFactory::CreateFromDescriptor(0, kTransportNoFlags);

  Ref<IOError> error = pump.Initialize();
  if (error) {
    fprintf(stderr, "init: %s\n", error->Message());
    return 1;
  }

  Ref<InStatus> status = new InStatus();

  error = pump.Register(mt.transport, status);
  if (error) {
    fprintf(stderr, "register: %s\n", error->Message());
    return 1;
  }

  while (true) {
    Ref<IOError> error = pump.Poll(0);
    if (error) {
      fprintf(stderr, "poll: %s\n", error->Message());
      return 1;
    }
  }
}
