#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sleep.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/Socket.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/TickCount.h>

#ifndef _WIN32
#define strtok_s strtok_r
#endif

using namespace Syncme;

Socket::Socket(SocketPair* pair, int handle, bool enableClose)
  : Pair(pair)
  , CH(Pair->GetChannel())
  , Handle(handle)
  , EnableClose(enableClose)
  , Configured(false)
  , CloseNotify(true)
  , BlockingMode(true)
  , LastError(SKT_ERROR::NONE)
{
}

Socket::~Socket()
{
  Close();

  CloseHandle(RxEvent);
  CloseHandle(BreakRead);
}

void Socket::SetLastError(SKT_ERROR e)
{
  LastError = e;
}

SKT_ERROR Socket::GetLastError() const
{
  return LastError;
}

int Socket::Detach(bool* enableClose)
{
  auto guard = Lock.Lock();

  int h = Handle;
  Handle = -1;

  if (enableClose)
    *enableClose = EnableClose;

  return h;
}

bool Socket::IsAttached() const
{
  return Handle != -1;
}

bool Socket::PeerDisconnected() const
{
  return Peer.Disconnected;
}

bool Socket::Attach(int socket, bool enableClose)
{
  auto guard = Lock.Lock();

  assert(Handle == -1);
  assert(socket != -1 || Pair->ClosePending);

  Handle = socket;
  EnableClose = enableClose;

  return true;
}

void Socket::Close()
{
  bool callClose = false;
  int socket = Detach(&callClose);

  if (socket != -1)
  {
    LogI("Closing");

    CloseHandle(RxEvent);

    if (callClose)
      closesocket(socket);
  }
}

bool Socket::Configure()
{
  if (Configured)
    return true;

  if (!SetOptions())
    return false;

  if (!SwitchToUnblockingMode())
    return false;

  Configured = true;
  return true;
}

bool Socket::SetOptions()
{  
  assert(Handle != -1 || Pair->ClosePending);

  if (Handle == -1)
    return false;

  ConfigPtr config = Pair->GetConfig();

  int rcvbuf = 0;
  socklen_t len = sizeof(rcvbuf);
  getsockopt(Handle, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, &len);

  int sndbuf = 0;
  len = sizeof(sndbuf);
  getsockopt(Handle, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, &len);
  LogI("Initial size of buffers rcv=%i, snd=%i", rcvbuf, sndbuf);

  int rcvsize = config->GetInt("rcvbuf", rcvbuf);
  if (rcvsize != rcvbuf)
  {
    if (setsockopt(Handle, SOL_SOCKET, SO_RCVBUF, (char*)&rcvsize, sizeof(rcvsize)))
    {
      LogosE("setsockopt(SO_RCVBUF) failed");
      return false;
    }

    LogI("new size of rcv buffer is %i", rcvsize);
  }

  int sndsize = config->GetInt("sndbuf", sndbuf);
  if (sndsize != sndbuf)
  {
    if (setsockopt(Handle, SOL_SOCKET, SO_SNDBUF, (char*)&sndsize, sizeof(sndsize)))
    {
      LogosE("setsockopt(SO_SNDBUF) failed");
      return false;
    }

    LogI("new size of snd buffer is %i", sndsize);
  }

  if (config->GetBool("tcp-nodelay", true))
  {
    int yes = 1;
    if (setsockopt(Handle, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes)))
    {
      LogosE("setsockopt(TCP_NODELAY) failed");
      return false;
    }
  }
  return true;
}

bool Socket::SwitchToUnblockingMode()
{
  assert(Handle != -1 || Pair->ClosePending);

  if (Handle == -1)
    return false;

  if (BreakRead == nullptr)
  {
    BreakRead = CreateSynchronizationEvent();
    if (BreakRead == nullptr)
    {
      LogE("CreateCommonEvent failed");
      return false;
    }
  }

  if (RxEvent == nullptr)
  {
    RxEvent = CreateSocketEvent(Handle, EVENT_READ | EVENT_CLOSE);
    if (RxEvent == nullptr)
    {
      LogE("CreateSocketEvent failed");
      return false;
    }

    unsigned long on = 1;
    int e = ioctlsocket(Handle, (int)FIONBIO, &on);
    if (e == -1)
    {
      LogosE("ioctlsocket(FIONBIO) failed");
      return false;
    }
  }

  BlockingMode = false;
  return true;
}

int Socket::WaitRxReady(int timeout)
{
  assert(RxEvent);

  auto start = GetTimeInMillisec();
  EventArray events(Pair->GetExitEvent(), Pair->GetCloseEvent(), RxEvent, BreakRead);

  const uint64_t delay = 1;

  for (uint64_t t0 = 0;;)
  {
    auto t = GetTimeInMillisec();
    if (t - t0 < delay)
      Sleep(1);

    t0 = t;

    uint32_t milliseconds = FOREVER;

    if (timeout != FOREVER)
    {
      if (t - start >= timeout)
      {
        SetLastError(SKT_ERROR::TIMEOUT);
        return 0;
      }

      milliseconds = uint32_t(start + timeout - t);
    }

    auto rc = WaitForMultipleObjects(events, false, milliseconds);
    if (rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1)
    {
      SetLastError(SKT_ERROR::CONNECTION_ABORTED);
      return -1;
    }

    if (rc == WAIT_RESULT::OBJECT_3)
    {
      SetLastError(SKT_ERROR::NONE);
      return 0;
    }

    if (rc == WAIT_RESULT::TIMEOUT)
    {
      SetLastError(Peer.Disconnected ? SKT_ERROR::GRACEFUL_DISCONNECT : SKT_ERROR::TIMEOUT);
      return 0;
    }

    if (rc == WAIT_RESULT::FAILED)
    {
      SetLastError(SKT_ERROR::CONNECTION_ABORTED);
      return 0;
    }

    int netev = GetSocketEvents(RxEvent);
    if (netev & EVENT_CLOSE)
    {
      Peer.Disconnected = true;

      int tout = timeout;
      timeout = Pair->PeerDisconnected();
      LogW("%s: peer disconnected. timeout %i -> %i", Pair->WhoAmI(this), tout, timeout);

      // We have to drain input buffer before closing socket
    }

    if (netev & EVENT_READ)
      break;
  }
  return 1;
}

bool Socket::StopPendingRead()
{
  return SetEvent(BreakRead);
}

int Socket::Read(std::vector<char>& buffer, int timeout)
{
  return Read(&buffer[0], buffer.size(), timeout);
}

int Socket::WriteStr(const std::string& str, int timeout)
{
  return Write(str.c_str(), str.length(), timeout);
}

int Socket::Write(const std::vector<char>& arr, int timeout)
{
  return Write(&arr[0], arr.size(), timeout);
}

int Socket::Write(const void* buffer, size_t size, int timeout)
{
  int n = 0;
  EventArray events(Pair->GetExitEvent(), Pair->GetCloseEvent());

  for (auto start = GetTimeInMillisec();;)
  {
    n = InternalWrite(buffer, size, timeout);
    if (n >= 0)
      break;

    SKT_ERROR e = GetError(n);
    if (e != SKT_ERROR::WOULDBLOCK)
      return n;

    auto rc = WaitForMultipleObjects(events, false, 10);
    if (rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1)
    {
      SetLastError(SKT_ERROR::CONNECTION_ABORTED);
      return -1;
    }

    if (timeout != FOREVER)
    {
      auto t = GetTimeInMillisec() - start;
      if (t > timeout)
      {
        SetLastError(SKT_ERROR::TIMEOUT);
        return 0;
      }
    }
  }
  return n;
}

bool Socket::InitPeer()
{
  union
  {
    char saddr4[INET_ADDRSTRLEN];
    char saddr6[INET6_ADDRSTRLEN];
  };

  assert(Handle != -1 || Pair->ClosePending);

  if (Handle == -1)
    return false;

  sockaddr_in addr4{};
  socklen_t cb = sizeof(addr4);
  if (getpeername(Handle, (sockaddr*)&addr4, &cb) != -1)
  {
    if (inet_ntop(AF_INET, &addr4.sin_addr, saddr4, sizeof(saddr4)) != nullptr)
    {
      Peer.IP = saddr4;
      Peer.Port = ntohs(addr4.sin_port);
      return true;
    }
  }

  sockaddr_in6 addr6{};
  cb = sizeof(addr6);
  if (getpeername(Handle, (sockaddr*)&addr6, &cb) == -1)
  {
    LogosE("getpeername failed");
    return false;
  }

  if (inet_ntop(AF_INET6, &addr6.sin6_addr, saddr6, INET6_ADDRSTRLEN) == nullptr)
  {
    LogosE("inet_ntop() failed");
    return false;
  }

  Peer.IP = saddr6;
  Peer.Port = ntohs(addr6.sin6_port);

  return true;
}

bool Socket::PeerFromHostString(
  const std::string& host
  , const std::string& scheme
)
{
  int port = 80;
  if (scheme == "https")
    port = 443;

  std::string temp(host);

  char* ctx = nullptr;
  const char* name = strtok_s(&temp[0], ": ", &ctx);
  const char* port_str = strtok_s(nullptr, " ", &ctx);
  if (port_str && *port_str)
  {
    char* e = nullptr;
    port = strtol(port_str, &e, 10);
    if (*e != '\0')
    {
      LogE("Invalid port value: %s", port_str);
      return false;
    }
  }

  struct addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  struct addrinfo* servinfo = nullptr;
  std::string p = std::to_string(port);
  int rc = getaddrinfo(name, p.c_str(), &hints, &servinfo);
  if (rc)
  {
    LogosE("getaddrinfo() failed");
    return false;
  }

  sockaddr_in& a = *(sockaddr_in*)servinfo->ai_addr;
  Peer.IP = inet_ntoa(a.sin_addr);
  Peer.Port = ntohs(a.sin_port);

  freeaddrinfo(servinfo);
  return true;
}

void Socket::AddLimit(int code, size_t count, uint64_t duration)
{
  auto guard = Lock.Lock();

  auto it = Limits.find(code);
  if (it != Limits.end())
  {
    it->second->SetLimit(count, duration);
    return;
  }

  ErrorLimitPtr ptr = std::make_shared<ErrorLimit>(count, duration);
  Limits[code] = ptr;
}

bool Socket::ReportError(int code)
{
  auto guard = Lock.Lock();

  auto it = Limits.find(code);
  if (it == Limits.end())
    return false;

  return it->second->ReportError();
}

std::string Socket::GetLastErrorStr() const
{
  switch (LastError)
  {
    case SKT_ERROR::NONE: return "NONE";
    case SKT_ERROR::TIMEOUT: return "TIMEOUT";
    case SKT_ERROR::GRACEFUL_DISCONNECT: return "GRACEFUL_DISCONNECT";
    case SKT_ERROR::WOULDBLOCK: return "WOULDBLOCK";
    case SKT_ERROR::IO_INCOMPLETE: return "IO_INCOMPLETE";
    case SKT_ERROR::CONNECTION_ABORTED: return "CONNECTION_ABORTED";
    case SKT_ERROR::GENERIC: return "GENERIC";
    default:
      break;
  }

  assert(!"unsupported error code");
  return std::string();
}