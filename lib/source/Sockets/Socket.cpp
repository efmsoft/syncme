#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sleep.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/Socket.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/TickCount.h>

#ifndef _WIN32
#define strtok_s strtok_r
#else
#include <mstcpip.h>
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
  , Pid(-1)
{
}

Socket::~Socket()
{
  Close();

  CloseHandle(RxEvent);
  CloseHandle(BreakRead);
}

void Socket::SetLastError(SKT_ERROR e, const char* file, int line)
{
  LastError.Set(e, file, line);
}

const SocketError& Socket::GetLastError() const
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

bool Socket::ReadPeerDisconnected()
{
  WaitRxReady(0);
  return PeerDisconnected();
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

    shutdown(socket, SD_RECEIVE);

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
  LogI("%s: Initial size of buffers rcv=%i, snd=%i", Pair->WhoAmI(this), rcvbuf, sndbuf);

  int rcvsize = config->GetInt("rcvbuf", rcvbuf);
  if (rcvsize != rcvbuf)
  {
    if (setsockopt(Handle, SOL_SOCKET, SO_RCVBUF, (char*)&rcvsize, sizeof(rcvsize)))
    {
      LogosE("setsockopt(SO_RCVBUF) failed");
      return false;
    }

    LogI("%s: new size of rcv buffer is %i", Pair->WhoAmI(this), rcvsize);
  }

  int sndsize = config->GetInt("sndbuf", sndbuf);
  if (sndsize != sndbuf)
  {
    if (setsockopt(Handle, SOL_SOCKET, SO_SNDBUF, (char*)&sndsize, sizeof(sndsize)))
    {
      LogosE("setsockopt(SO_SNDBUF) failed");
      return false;
    }

    LogI("%s: new size of snd buffer is %i", Pair->WhoAmI(this), sndsize);
  }

  if (config->GetBool("tcp-nodelay", true))
  {
    int yes = 1;
    if (setsockopt(Handle, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes)))
    {
      LogosE("setsockopt(TCP_NODELAY) failed");
      return false;
    }

    LogI("%s: TCP_NODELAY was set to true", Pair->WhoAmI(this));
  }

#ifdef _WIN32
  ULONG delay = (ULONG)config->GetInt("keepalive-delay", 15000);
  if (delay)
  {
    struct tcp_keepalive keepalive_vals = {
        1,      // TCP keep-alive on.
        delay,  // Delay seconds before sending first TCP keep-alive packet.
        delay,  // Delay seconds between sending TCP keep-alive packets.
    };

    DWORD bytes_returned = 0xABAB;
    int rv = WSAIoctl(
      Handle
      , SIO_KEEPALIVE_VALS
      , &keepalive_vals
      , sizeof(keepalive_vals)
      , nullptr
      , 0
      , &bytes_returned
      , nullptr
      , nullptr
    );

    if (rv == SOCKET_ERROR)
    {
      LogosE("WSAIoctl(SIO_KEEPALIVE_VALS) failed");
      return false;
    }

    LogI("%s: keepalive-delay set to %i ms", Pair->WhoAmI(this), int(delay));
  }
#endif
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

    LogI("Unblocking mode is switched on");
  }

  BlockingMode = false;
  return true;
}

int Socket::WaitRxReady(int timeout)
{
  assert(RxEvent);

  const int LITTLE_WAIT = 10;
  if (Peer.Disconnected)
    timeout = LITTLE_WAIT;

  auto start = GetTimeInMillisec();
  EventArray events(Pair->GetExitEvent(), Pair->GetCloseEvent(), RxEvent, BreakRead);

  for (int loops = 0;; ++loops)
  {
    auto t = GetTimeInMillisec();
    uint32_t milliseconds = FOREVER;

    if (timeout != FOREVER)
    {
      if (t - start >= timeout)
      {
        SKT_SET_LAST_ERROR2(Peer.Disconnected ? SKT_ERROR::GRACEFUL_DISCONNECT : SKT_ERROR::TIMEOUT);
        return 0;
      }

      milliseconds = uint32_t(start + timeout - t);
    }

    auto rc = WaitForMultipleObjects(events, false, milliseconds);
    if (rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1)
    {
      SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      return -1;
    }

    if (rc == WAIT_RESULT::OBJECT_3)
    {
      LogI("Break read");
      SKT_SET_LAST_ERROR(NONE);
      return 0;
    }

    if (rc == WAIT_RESULT::TIMEOUT)
    {
      t = GetTimeInMillisec();
      if (t - start >= timeout)
      {
        SKT_SET_LAST_ERROR2(Peer.Disconnected ? SKT_ERROR::GRACEFUL_DISCONNECT : SKT_ERROR::TIMEOUT);
        return 0;
      }

      continue;
    }

    if (rc == WAIT_RESULT::FAILED)
    {
      SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      return -1;
    }

    int netev = GetSocketEvents(RxEvent);
    if (netev & EVENT_CLOSE)
    {
      Peer.Disconnected = true;
      Peer.When = GetTimeInMillisec();

      int tout = timeout;
      timeout = 0;
      
      LogW("%s: peer disconnected. timeout %i -> %i", Pair->WhoAmI(this), tout, timeout);

      // We have to drain input buffer before closing socket
    }

    if (netev & EVENT_READ)
      break;
  }

  SKT_SET_LAST_ERROR(NONE);
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
  assert(size > 0);

  int n = 0;
  EventArray events(Pair->GetExitEvent(), Pair->GetCloseEvent());

  for (auto start = GetTimeInMillisec();;)
  {
    n = InternalWrite(buffer, size, timeout);
    if (n > 0)
      break;

    SKT_ERROR e = Ossl2SktError(n);
    if (e != SKT_ERROR::WOULDBLOCK)
    {
      SKT_SET_LAST_ERROR2(e);
      return n;
    }

    auto rc = WaitForMultipleObjects(events, false, 10);
    if (rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1)
    {
      SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      return -1;
    }

    if (timeout != FOREVER)
    {
      auto t = GetTimeInMillisec() - start;
      if (t > timeout)
      {
        SKT_SET_LAST_ERROR(TIMEOUT);
        return 0;
      }
    }
  }
  
  SKT_SET_LAST_ERROR(NONE);
  return n;
}

void Socket::Unread(const char* p, size_t n)
{
  PacketPtr packet = std::make_shared<Packet>(n);
  memcpy(&(*packet.get())[0], p, n);
  Packets.push_back(packet);
}

int Socket::ReadPacket(void* buffer, size_t size)
{
  if (Packets.empty())
    return 0;
  
  SKT_SET_LAST_ERROR(NONE);

  PacketPtr p = Packets.front();
  Packets.pop_front();

  memcpy(buffer, &(*p.get())[0], int(p->size()));
  return int(p->size());
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
