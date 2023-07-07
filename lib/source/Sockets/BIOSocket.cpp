#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/BIOSocket.h>
#include <Syncme/Sockets/SocketPair.h>

using namespace Syncme;

BIOSocket::BIOSocket(SocketPair* pair)
  : Socket(pair)
  , Bio(nullptr)
{
}

BIOSocket::~BIOSocket()
{
  if (Bio)
    BIO_free(Bio);
}

bool BIOSocket::Attach(int socket, bool enableClose)
{
  auto guard = Lock.Lock();

  bool f = Socket::Attach(socket, enableClose);
  if (!f)
    return false;

  assert(Bio == nullptr);

  Bio = BIO_new_socket(int(Handle), BIO_NOCLOSE);
  if (!Bio)
  {
    LogE("BIO_new_socket() failed");

    Socket::Detach();
    return false;
  }

  return true;
}

void BIOSocket::Shutdown()
{
  if (CloseNotify && !PeerDisconnected())
    BIO_shutdown_wr(Bio);
}

int BIOSocket::InternalRead(void* buffer, size_t size, int timeout)
{
  std::lock_guard<std::mutex> guard(BioLock);
  int n = BIO_read(Bio, buffer, int(size));

  if (n <= 0)
  {
    if (BIO_should_retry(Bio))
    {
      if (BIO_should_read(Bio) || BIO_should_write(Bio) || BIO_should_io_special(Bio))
      {
        SetLastError(Peer.Disconnected ? SKT_ERROR::GRACEFUL_DISCONNECT : SKT_ERROR::TIMEOUT);
        if (n < 0)
          n = 0;
      }
      else
      {
        SetLastError(SKT_ERROR::IO_INCOMPLETE);
        CloseNotify = false;
        n = -1;
      }
    }
    else
    {
      SetLastError(SKT_ERROR::IO_INCOMPLETE);
      CloseNotify = false;
      n = -1;
    }
  }
  return n;
}

int BIOSocket::Read(void* buffer, size_t size, int timeout)
{
  int n = ReadPacket(buffer, size);
  if (n)
    return n;

  n = InternalRead(buffer, size, timeout);
  if (n > 0 || n < 0)
    return n;

  n = WaitRxReady(timeout);
  if (n <= 0)
    return n;

  return InternalRead(buffer, size, timeout);
}

int BIOSocket::InternalWrite(const void* buffer, size_t size, int timeout)
{
  std::lock_guard<std::mutex> guard(BioLock);
  int n = BIO_write(Bio, buffer, int(size));

  if (n <= 0)
  {
    if (BIO_should_retry(Bio))
    {
      if (BIO_should_read(Bio) || BIO_should_write(Bio) || BIO_should_io_special(Bio))
      {
        SetLastError(Peer.Disconnected ? SKT_ERROR::GRACEFUL_DISCONNECT : SKT_ERROR::TIMEOUT);
        if (n < 0)
          n = 0;
      }
      else
      {
        SetLastError(SKT_ERROR::IO_INCOMPLETE);
        CloseNotify = false;
        n = -1;
      }
    }
    else
    {
      SetLastError(SKT_ERROR::IO_INCOMPLETE);
      CloseNotify = false;
      n = -1;
    }
  }
  return n;
}

int BIOSocket::GetFD() const
{
  int socket = 0;
  BIO_get_fd(Bio, &socket);

  return socket;
}

SKT_ERROR BIOSocket::GetError(int ret) const
{
  return GetLastError();
}

void BIOSocket::LogIoError(const char* fn, const char* text)
{
  if (Pair->Closing())
    return;

#ifdef USE_LOGME
  Logme_If(
    true
    , Logme::Instance
    , Logme::Level::LEVEL_ERROR
    , "%s%s. Error: %s"
    , fn
    , text
    , GetLastErrorStr().c_str()
  );
#endif
}