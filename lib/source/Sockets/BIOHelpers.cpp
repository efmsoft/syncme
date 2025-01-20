#include <Syncme/Sockets/BIOHelpers.h>
#include <Syncme/Sockets/BIOSocket.h>

const char* Syncme::BIOperationName(int op)
{
  switch (op & 0xF)
  {
  case BIO_CB_FREE: return "FREE";
  case BIO_CB_READ: return "READ";
  case BIO_CB_WRITE: return "WRITE";
  case BIO_CB_PUTS: return "PUTS";
  case BIO_CB_GETS: return "GETS";
  case BIO_CB_CTRL: return "CTRL";
  case BIO_CB_RECVMMSG: return "RECVMMSG";
  case BIO_CB_SENDMMSG: return "SENDMMSG";
  default:
    break;
  }
  return "???";
}
