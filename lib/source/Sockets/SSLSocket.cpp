#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/Sockets/SSLHelpers.h>
#include <Syncme/Sockets/SSLSocket.h>
#include <Syncme/TickCount.h>

#ifdef _WIN32
#include <windows.h>
#endif

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
  const uint64_t limit = 3000;

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

int SSLSocket::Read(void* buffer, size_t size, int timeout)
{
  int n = ReadPacket(buffer, size);
  if (n)
    return n;

  do
  {
    if (true)
    {
      std::lock_guard<std::mutex> guard(SslLock);

      if (SSL_has_pending(Ssl))
      {
        n = SSL_read(Ssl, buffer, int(size));

        // Note that it is possible for SSL_has_pending() to return 1, 
        // and then a subsequent call to SSL_read_ex() or SSL_read() to 
        // return no data because the unprocessed buffered data when processed 
        // yielded no application data (for example this can happen during 
        // renegotiation). It is also possible in this scenario for 
        // SSL_has_pending() to continue to return 1 even after an SSL_read_ex() 
        // or SSL_read() call because the buffered and unprocessed data is not yet 
        // processable (e.g. because OpenSSL has only received a partial record so far).

        // if n == 0, wait for data during 'timeout'
        if (n > 0)
          break;

        if (n < 0)
        {
          auto err = GetError(n);
          if (err == SKT_ERROR::NONE || err == SKT_ERROR::WOULDBLOCK)
            break;

          int e = SSL_get_error(Ssl, n);
          LogE("SSL_read() returned error %s", SslError(e).c_str());
          CloseNotify = false;
          return -1;
         
        }
      }
    }

    n = WaitRxReady(timeout);
    if (n <= 0)
      return n;

    std::lock_guard<std::mutex> guard(SslLock);
    n = SSL_read(Ssl, buffer, int(size));

  } while (false);

  if (n == 0 && Peer.Disconnected)
  {
    SetLastError(Peer.Disconnected ? SKT_ERROR::GRACEFUL_DISCONNECT : SKT_ERROR::NONE);
    return 0;
  }

  if (n == -1)
  {
    auto err = GetError(n);
    if (err == SKT_ERROR::NONE || err == SKT_ERROR::WOULDBLOCK)
    {
      SetLastError(Peer.Disconnected ? SKT_ERROR::GRACEFUL_DISCONNECT : SKT_ERROR::NONE);
      return 0;
    }

    int e = SSL_get_error(Ssl, n);
    LogE("SSL_read() returned error %s", SslError(e).c_str());
    CloseNotify = false;
    return -1;
  }
  return n;
}

int SSLSocket::InternalWrite(const void* buffer, size_t size, int timeout)
{
  std::lock_guard<std::mutex> guard(SslLock);
  return SSL_write(Ssl, buffer, int(size));
}

SKT_ERROR SSLSocket::GetError(int ret) const
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
    , GetBioError().c_str()
  );
#endif
}
