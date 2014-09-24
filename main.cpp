// vim: set ts=8 sw=2 sts=2 tw=99 et:
#include <stdio.h>
#include <stdlib.h>
#include <amio.h>
#include "posix/amio-select-pump.h"

using namespace ke;
using namespace amio;

int main()
{
  SelectMessagePump pump;
  Ref<Transport> transport = TransportFactory::CreateFromDescriptor(0, kTransportNoFlags);

  if (!pump.Register(transport, nullptr))
    abort();

  while (pump.Poll(nullptr))
    ;
}
