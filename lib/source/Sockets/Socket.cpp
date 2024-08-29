#include <cassert>

#include <Syncme/Event/Event.h>
#include <Syncme/Logger/Log.h>
#include <Syncme/Sleep.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/Socket.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/TickCount.h>

#ifndef _WIN32
#define strtok_s strtok_r
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#else
#include <mstcpip.h>
#endif

using namespace Syncme;
using namespace std::placeholders;


Socket::Socket(SocketPair* pair, int handle, bool enableClose)
  : Pair(pair)
  , CH(Pair->GetChannel())
  , Handle(handle)
  , EnableClose(enableClose)
  , Configured(false)
  , CloseNotify(true)
  , BlockingMode(true)
  , AcceptPort(0)
  , Pid(-1)
#if SKTEPOLL
  , Poll(-1)
  , EventDescriptor(-1)
  , EventsMask(0)
  , ExitEventCookie(0)
  , CloseEventCookie(0)
  , BreakEventCookie(0)
#endif
{
#if SKTEPOLL
  Poll = epoll_create(1);
  if (Poll == -1)
  {
    LogosE("epoll_create failed");
    return;
  }

  EventDescriptor = eventfd(0, EFD_NONBLOCK);
  if (EventDescriptor == -1)
  {
    LogosE("eventfd failed");
    return;
  }

  epoll_event ev{};
  ev.data.fd = EventDescriptor;
  ev.events |= EPOLLIN;

  if (epoll_ctl(Poll, EPOLL_CTL_ADD, EventDescriptor, &ev) == -1)
  {
    LogosE("epoll_ctl(EPOLL_CTL_ADD) failed for EventDescriptor");
    return;
  }
#endif  
}

Socket::~Socket()
{
  Close();

  CloseHandle(RxEvent);

#if SKTEPOLL
  if (BreakRead)
    BreakRead->UnregisterWait(BreakEventCookie);
#endif

  CloseHandle(BreakRead);

#if SKTEPOLL
  if (EventDescriptor != -1)
  {
    epoll_event ev{};
    ev.data.fd = EventDescriptor;
    ev.events |= EPOLLIN;
    if (epoll_ctl(Poll, EPOLL_CTL_DEL, EventDescriptor, &ev) == -1)
    {
      LogosE("epoll_ctl(EPOLL_CTL_DEL) failed for EventDescriptor");
    }
  }

  if (Poll != -1)
  {
    if (close(Poll) == -1)
    {
      LogosE("close(Poll) failed");
    }

    Poll = -1;
  }

  if (EventDescriptor != -1)
  {
    if (close(EventDescriptor) == -1)
    {
      LogosE("close(EventDescriptor) failed");
    }

    EventDescriptor = -1;
  }
#endif
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

#if SKTEPOLL
  if (Handle != -1)
  {
    if (ExitEventCookie)
    {
      Pair->GetExitEvent()->UnregisterWait(ExitEventCookie);
      ExitEventCookie = 0;
    }

    if (CloseEventCookie)
    {
      Pair->GetCloseEvent()->UnregisterWait(CloseEventCookie);
      CloseEventCookie = 0;
    }

    epoll_event ev{};
    ev.data.fd = Handle;
    ev.events |= EPOLLIN;
    if (epoll_ctl(Poll, EPOLL_CTL_DEL, Handle, &ev) == -1)
    {
      LogosE("epoll_ctl(EPOLL_CTL_DEL) failed for Handle");
    }
  }
#endif

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

#if SKTEPOLL
  ExitEventCookie = Pair->GetExitEvent()->RegisterWait(
    std::bind(&Socket::EventSignalled, this, WAIT_RESULT::OBJECT_0, _1, _2)
  );

  CloseEventCookie = Pair->GetCloseEvent()->RegisterWait(
    std::bind(&Socket::EventSignalled, this, WAIT_RESULT::OBJECT_1, _1, _2)
  );

  epoll_event ev{};
  ev.data.fd = Handle;
  ev.events = EPOLLIN | EPOLLRDHUP;

  if (epoll_ctl(Poll, EPOLL_CTL_ADD, Handle, &ev) == -1)
  {
    LogosE("epoll_ctl(EPOLL_CTL_ADD) failed for Handle");
  }
#endif

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

bool Socket::SwitchToBlockingMode()
{
  assert(Handle != -1 || Pair->ClosePending);

  if (Handle == -1)
    return false;

  if (BlockingMode)
    return true;

  if (BreakRead)
  {
#if SKTEPOLL
    BreakRead->UnregisterWait(BreakEventCookie);
#endif
    CloseHandle(BreakRead);
  }

  if (RxEvent)
  {
#ifdef _WIN32
    // To set socket s back to blocking mode, it is first 
    // necessary to clear the event record associated with 
    // socket s via a call to WSAEventSelect with lNetworkEvents 
    // set to zero and the hEventObject parameter set to NULL. 
    // You can then call ioctlsocket or WSAIoctl to set the 
    // socket back to blocking mode.
    WSAEventSelect(Handle, 0, 0);
#endif

    CloseHandle(RxEvent);
  }
  
  unsigned long on = 0;
  int e = ioctlsocket(Handle, (int)FIONBIO, &on);
  if (e == -1)
  {
    LogosE("ioctlsocket(FIONBIO) failed");
    return false;
  }

  LogI("Blocking mode is switched on");
  
  BlockingMode = true;
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
#if SKTEPOLL
    BreakEventCookie = BreakRead->RegisterWait(
      std::bind(&Socket::EventSignalled, this, WAIT_RESULT::OBJECT_3, _1, _2)
    );
#endif
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

#if SKTEPOLL
void Socket::EventSignalled(WAIT_RESULT r, uint32_t cookie, bool failed)
{
#if SKTEPOLL
  // write() will force epoll_wait to exit
  // we have to write a value > 0
  uint64_t value = uint64_t(r) + 1;
  auto s = write(EventDescriptor, &value, sizeof(value));
  if (s != sizeof(value))
  {
    LogE("write failed");
  }
#endif
}

WAIT_RESULT Socket::FastWaitForMultipleObjects(int timeout)
{
  //
  // If there data to read on the socked, just return OBJECT_2
  //
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(Handle, &rfds);

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  if (select(Handle + 1, &rfds, nullptr, nullptr, &tv) > 0)
  {
    if (FD_ISSET(Handle, &rfds))
    {
      EventsMask |= EVENT_READ;
      return WAIT_RESULT::OBJECT_2;
    }
  }

  epoll_event events[2]{};
  int n = epoll_wait(Poll, &events[0], 2, timeout);
  int en = errno;
  
  if (n < 0)
  {
    if (en != EINTR)
    {
      LogE("epoll_wait failed. Error %i", en);
      return WAIT_RESULT::FAILED;
    }

    n = 0;
  }

  WAIT_RESULT result = WAIT_RESULT::TIMEOUT;

  for (int i = 0; i < n; ++i)
  {
    epoll_event& e = events[i];

    if (e.data.fd == EventDescriptor)
    {
      uint64_t value = 0;
      auto n = read(EventDescriptor, &value, sizeof(value));
      if (n != sizeof(value))
      {
        LogosE("read failed");
      }
      else
      {
        result = WAIT_RESULT(value - 1);
      }

      value = 0;
      n = write(EventDescriptor, &value, sizeof(value));
      if (n != sizeof(value))
      {
        LogosE("write failed");
      }
    }
    else if (e.data.fd == Handle)
    {
      if (e.events & EPOLLIN)
        EventsMask |= EVENT_READ;

      if (e.events & EPOLLRDHUP)
        EventsMask |= EVENT_CLOSE;

      result = WAIT_RESULT::OBJECT_2;
    }
  }

  return result;
}
#endif

int Socket::WaitRxReady(int timeout)
{
  assert(RxEvent);

  const int LITTLE_WAIT = 10;
  if (Peer.Disconnected)
    timeout = LITTLE_WAIT;

  auto start = GetTimeInMillisec();
  
#if SKTEPOLL == 0
  EventArray events(
    Pair->GetExitEvent()
    , Pair->GetCloseEvent()
    , RxEvent
    , BreakRead
  );
#endif

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

#if SKTEPOLL
    auto rc = FastWaitForMultipleObjects(milliseconds);
#else
    auto rc = WaitForMultipleObjects(events, false, milliseconds);
#endif

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

#if SKTEPOLL
    int netev = EventsMask;
    EventsMask = 0;
#else
    int netev = GetSocketEvents(RxEvent);
#endif

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

bool Socket::InitAcceptAddress()
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
  if (getsockname(Handle, (sockaddr*)&addr4, &cb) != -1)
  {
    if (inet_ntop(AF_INET, &addr4.sin_addr, saddr4, sizeof(saddr4)) != nullptr)
    {
      AcceptIP = saddr4;
      AcceptPort = htons(addr4.sin_port);
      return true;
    }
  }

  sockaddr_in6 addr6{};
  cb = sizeof(addr6);
  if (getsockname(Handle, (sockaddr*)&addr6, &cb) == -1)
  {
    LogosE("getsockname failed");
    return false;
  }

  if (inet_ntop(AF_INET6, &addr6.sin6_addr, saddr6, INET6_ADDRSTRLEN) == nullptr)
  {
    LogosE("inet_ntop() failed");
    return false;
  }

  AcceptIP = saddr6;
  AcceptPort = htons(addr4.sin_port);
  return true;
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
  
  InitAcceptAddress();

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
  , int af
  , int type
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

  for (struct addrinfo* addr = servinfo; addr != NULL; addr = addr->ai_next)
  {
    if (addr->ai_family != af || addr->ai_socktype != type)
      continue;

    bool ok = true;
    int sd = (int)socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sd != -1)
    {
      ok = false;
      if (connect(sd, addr->ai_addr, (int)addr->ai_addrlen) == 0)
        ok = true;

      closesocket(sd);
    }

    if (ok)
    {
      sockaddr_in& a = *(sockaddr_in*)addr->ai_addr;
      Peer.IP = inet_ntoa(a.sin_addr);
      Peer.Port = ntohs(a.sin_port);
      break;
    }
  }

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
