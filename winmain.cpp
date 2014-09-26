// vim: set ts=8 sw=2 sts=2 tw=99 et:
#include <stdio.h>
#include <stdlib.h>
#include <amio-windows.h>
//#include <amio-posix.h>
//#include "posix/amio-posix-select.h"
//#include "linux/amio-linux-epoll.h"
//#include "bsd/amio-bsd-kqueue.h"

using namespace ke;
using namespace amio;

int main()
{
  Ref<Poller> poller;
  Ref<IOError> error = PollerFactory::CreatePoller(&poller);
  if (*error) {
    fprintf(stderr, "epoll: %s\n", error->Message());
    return 1;
  }

  Ref<Transport> transport;
  error = TransportFactory::CreateFromHandle(&transport, GetStdHandle(STD_INPUT_HANDLE), kTransportNoAutoClose);
  if (*error) {
    fprintf(stderr, "transport: %s\n", error->Message());
    return 1;
  }

//  Ref<InStatus> status = new InStatus();

//  error = poller->Register(transport, status);
//  if (error) {
//    fprintf(stderr, "register: %s\n", error->Message());
//    return 1;
//  }

//  while (true) {
//    Ref<IOError> error = poller->Poll(0);
//    if (error) {
//      fprintf(stderr, "poll: %s\n", error->Message());
//      return 1;
//    }
//  }
}
