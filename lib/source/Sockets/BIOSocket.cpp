#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/BIOSocket.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/TickCount.h>

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

  if (Bio)
    BIO_free(Bio);

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
  SKT_SET_LAST_ERROR(NONE);

  std::lock_guard<std::mutex> guard(BioLock);
  int n = BIO_read(Bio, buffer, int(size));

  if (n == 0)
  {
    SKT_SET_LAST_ERROR(WOULDBLOCK);
    return 0;
  }

  if (n < 0)
  {
    if (BIO_should_retry(Bio))
    {
      SKT_SET_LAST_ERROR(WOULDBLOCK);
      return 0;
    }

    SKT_SET_LAST_ERROR(IO_INCOMPLETE);
    CloseNotify = false;
    n = -1;
  }

  return n;
}

int BIOSocket::Read(void* buffer, size_t size, int timeout)
{
  SKT_SET_LAST_ERROR(NONE);

  int n = ReadPacket(buffer, size);
  if (n)
    return n;

  for (auto start = GetTimeInMillisec();;)
  {
    uint32_t ms = FOREVER;
    if (timeout != FOREVER)
    {
      auto t = GetTimeInMillisec();

      if (t - start > timeout)
      {
        SKT_SET_LAST_ERROR(TIMEOUT);
        return 0;
      }

      ms = uint32_t(start + timeout - t);
    }

    n = WaitRxReady(ms);
    if (n < 0)
      return n;

    if (n == 0)
    {
      auto& e = LastError;
      assert(e == SKT_ERROR::NONE || e == SKT_ERROR::TIMEOUT || e == SKT_ERROR::GRACEFUL_DISCONNECT);
      return 0;
    }

    n = InternalRead(buffer, size, ms);
    if (n != 0)
      break;

    if (Peer.Disconnected)
    {
      SKT_SET_LAST_ERROR(GRACEFUL_DISCONNECT);
      break;
    }
  }

  return n;
}

int BIOSocket::InternalWrite(const void* buffer, size_t size, int timeout)
{
  std::lock_guard<std::mutex> guard(BioLock);
  int n = BIO_write(Bio, buffer, int(size));

  if (n == 0)
  {
    SKT_SET_LAST_ERROR(WOULDBLOCK);
    return 0;
  }

  if (n < 0)
  {
    if (BIO_should_retry(Bio))
    {
      SKT_SET_LAST_ERROR(WOULDBLOCK);
      return 0;
    }

    SKT_SET_LAST_ERROR(IO_INCOMPLETE);
    CloseNotify = false;
    n = -1;
  }

  return n;
}

int BIOSocket::GetFD() const
{
  int socket = 0;
  BIO_get_fd(Bio, &socket);

  return socket;
}

SKT_ERROR BIOSocket::Ossl2SktError(int ret) const
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
    , GetLastError().Format().c_str()
  );
#endif
}