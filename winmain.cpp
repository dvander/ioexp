// vim: set ts=8 sw=2 sts=2 tw=99 et:
#include <stdio.h>
#include <stdlib.h>
#include <amio-windows.h>
#include <WinSock2.h>
//#include <amio-posix.h>
//#include "posix/amio-posix-select.h"
//#include "linux/amio-linux-epoll.h"
//#include "bsd/amio-bsd-kqueue.h"

using namespace ke;
using namespace amio;

class InStatus : public IOListener
{
 public:
};

int main()
{
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);

  Ref<Poller> poller;
  Ref<IOError> error = PollerFactory::CreatePoller(&poller);
  if (error) {
    fprintf(stderr, "epoll: %s\n", error->Message());
    return 1;
  }

  HANDLE hFile = CreateFileA("C:\\users\\dvander\\syncrpc.patch", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "cfile: %d\n", GetLastError());
    return 1;
  }

  Ref<Transport> transport;
  error = TransportFactory::CreateFromFile(&transport, hFile, kTransportNoAutoClose);
  if (error) {
    fprintf(stderr, "transport: %s\n", error->Message());
    return 1;
  }

  Ref<InStatus> status = new InStatus();

  error = poller->Attach(transport, status);
  if (error) {
    fprintf(stderr, "register: %s\n", error->Message());
    return 1;
  }

  char buffer[256];
  IOResult r = transport->Read(buffer, sizeof(buffer), 0x1337);

  while (true) {
    Ref<IOError> error = poller->Poll(kNoTimeout);
    if (error) {
      fprintf(stderr, "poll: %s\n", error->Message());
      return 1;
    }
  }
}
