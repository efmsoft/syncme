#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/Sockets/SSLHelpers.h>
#include <Syncme/Sockets/SSLSocket.h>
#include <Syncme/TickCount.h>

#ifdef _WIN32
#include <windows.h>
#endif

#pragma warning(disable : 6262)

using namespace Syncme;

SSLSocket::SSLSocket(SocketPair* pair, SSL* ssl)
  : Socket(pair)
  , Ssl(ssl)
{
  assert(Ssl);
}

SSLSocket::~SSLSocket()
{
}

int SSLSocket::GetFD() const
{
  return SSL_get_fd(Ssl);
}

void SSLSocket::Shutdown()
{
  LogI("Shutting down connection...");

  if (Ssl == nullptr || BlockingMode)
    return;

  if (SSL_in_init(Ssl))
    return;

  auto start = GetTimeInMillisec();
  const uint64_t limit = 100;

  const char* whoami = Pair->WhoAmI(this);

  // OpenSSL help:
  // Note that SSL_shutdown() must not be called if a previous fatal error 
  // has occurred on a connection i.e. if SSL_get_error() has returned 
  // SSL_ERROR_SYSCALL or SSL_ERROR_SSL.
  if (!CloseNotify || PeerDisconnected())
  {
    LogI("%s: skip SSL_shutdown call", whoami);
    return;
  }

  EventArray events(Pair->GetExitEvent(), Pair->GetCloseEvent());

  const int small_delay = 10; // 10 ms

  int n, e;
  while (true)
  {
    auto spent = GetTimeInMillisec() - start;
    if (spent >= limit)
      break;

    if (true)
    {
      std::lock_guard<std::mutex> guard(SslLock);

      n = SSL_shutdown(Ssl);
      e = SSL_get_error(Ssl, n);
    }

    if (n > 0 || (n < 0 && (e == SSL_ERROR_ZERO_RETURN || e == SSL_ERROR_NONE)))
    {
      // The shutdown was successfully completed. The "close notify" 
      // alert was sent and the peer's "close notify" alert was received.
      LogI("%s connection closed gracefully", whoami);
      break;
    }

    auto rc = WaitForMultipleObjects(events, false, small_delay);
    if (rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1)
    {
      LogI("%s: gracefull disconnect aborted", whoami);
      break;
    }

    if (n == 0)
    {
      // The shutdown is not yet finished. Call SSL_shutdown() for a 
      // second time, if a bidirectional shutdown shall be performed.
      // The output of SSL_get_error() may be misleading, as an 
      // erroneous SSL_ERROR_SYSCALL may be flagged even though no 
      // error occurred.
      continue;
    }

    // The shutdown was not successful because a fatal error occurred
    // either at the protocol level or a connection failure occurred.
    // It can also occur if action is need to continue the operation 
    // for non - blocking BIOs. Call SSL_get_error() with the return 
    // value ret to find out the reason.

    if (e == SSL_ERROR_WANT_READ)
    {
      char buffer[64 * 1024]{};

      std::lock_guard<std::mutex> guard(SslLock);
      int n = SSL_read(Ssl, buffer, int(sizeof(buffer)));

      if (n > 0)
      {
        LogE("Peer sent %i bytes of data during shutdown", n);
      }
    }


    bool err =
      e != SSL_ERROR_NONE
      && e != SSL_ERROR_ZERO_RETURN
      && e != SSL_ERROR_WANT_READ
      && e != SSL_ERROR_WANT_WRITE;

    if (err)
    {
      LogI("%s: gracefull disconnect stopped. Error %i", whoami, e);
      break;
    }
  }
}

int SSLSocket::ReadPending(void* buffer, size_t size)
{
  std::lock_guard<std::mutex> guard(SslLock);

  if (!SSL_has_pending(Ssl))
    return 0;
  
  int n = SSL_read(Ssl, buffer, int(size));

  // Note that it is possible for SSL_has_pending() to return 1, 
  // and then a subsequent call to SSL_read_ex() or SSL_read() to 
  // return no data because the unprocessed buffered data when processed 
  // yielded no application data (for example this can happen during 
  // renegotiation). It is also possible in this scenario for 
  // SSL_has_pending() to continue to return 1 even after an SSL_read_ex() 
  // or SSL_read() call because the buffered and unprocessed data is not yet 
  // processable (e.g. because OpenSSL has only received a partial record so far).

  // if n == 0, wait for data during 'timeout'

  if (n < 0)
    return TranslateSSLError(n, "SSL_read");

  SKT_SET_LAST_ERROR(NONE);
  return n;
}

int SSLSocket::Read(void* buffer, size_t size, int timeout)
{
  SKT_SET_LAST_ERROR(NONE);

  int n = ReadPacket(buffer, size);
  if (n)
    return n;

  for (auto start = GetTimeInMillisec();;)
  {
    n = ReadPending(buffer, size);
    if (n != 0)                             // If error or we read some data
      return n;

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
      assert(
        e == SKT_ERROR::NONE 
        || e == SKT_ERROR::TIMEOUT 
        || e == SKT_ERROR::GRACEFUL_DISCONNECT
        || e == SKT_ERROR::GENERIC
      );

      return 0;
    }

    std::lock_guard<std::mutex> guard(SslLock);
    n = SSL_read(Ssl, buffer, int(size));
    n = TranslateSSLError(n, "SSL_read");

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

int SSLSocket::InternalWrite(const void* buffer, size_t size, int timeout)
{
  std::lock_guard<std::mutex> guard(SslLock);

  int n = SSL_write(Ssl, buffer, int(size));
  return TranslateSSLError(n, "SSL_write");
}

int SSLSocket::InternalRead(void* buffer, size_t size, int timeout)
{
  std::lock_guard<std::mutex> guard(SslLock);

  int n = SSL_read(Ssl, buffer, int(size));
  return TranslateSSLError(n, "SSL_read");
}

int SSLSocket::TranslateSSLError(int n, const char* method)
{
  if (n >= 0)
  {
    SKT_SET_LAST_ERROR(NONE);
    return n;
  }

  auto err = Ossl2SktError(n);
  SKT_SET_LAST_ERROR2(err);

  if (err == SKT_ERROR::NONE || err == SKT_ERROR::WOULDBLOCK)
    return 0;

  int e = SSL_get_error(Ssl, n);
  LogE("%s returned error %s: %s", method, SslError(e).c_str(), GetBioError().c_str());

  CloseNotify = false;
  return n;
}

SKT_ERROR SSLSocket::Ossl2SktError(int ret) const
{
  int err = SSL_get_error(Ssl, ret);
  switch (err)
  {
  case SSL_ERROR_NONE:
  case SSL_ERROR_ZERO_RETURN:
    return SKT_ERROR::NONE;

  case SSL_ERROR_WANT_WRITE:
  case SSL_ERROR_WANT_READ:
    return SKT_ERROR::WOULDBLOCK;

  case SSL_ERROR_SYSCALL:
#ifdef _WIN32
    if (::GetLastError() == WSAEWOULDBLOCK)
      return SKT_ERROR::WOULDBLOCK;
#else
    if (errno == EWOULDBLOCK)
      return SKT_ERROR::WOULDBLOCK;
#endif
    break;

  default:
    break;
  }

  return SKT_ERROR::GENERIC;
}

void SSLSocket::LogIoError(const char* fn, const char* text)
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
