#include <cassert>

#include <Syncme/Event/Event.h>
#include <Syncme/Logger/Log.h>
#include <Syncme/ProcessThreadId.h>
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

std::mutex Socket::TotalsLock;
IOCounters Socket::Totals{};

const char* Socket::EvName[5] =
{
  "evSocket", "evTX", "evBreak", "evExit", "evStop"
};

static bool isIPv4MappedIPv6(const sockaddr_in6& saddr6)
{
  // Check for IPv4-mapped IPv6 format (::ffff:0:0/96 or ::ffff:a.b.c.d)
  const unsigned char ipv4MappedPrefix[12] =
  {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff
  };

  const unsigned char* addr_bytes;
#ifdef _WIN32
  addr_bytes = saddr6.sin6_addr.u.Byte;
#else
  addr_bytes = saddr6.sin6_addr.s6_addr;
#endif
    
  return memcmp(addr_bytes, ipv4MappedPrefix, 12) == 0;    
}

static bool readSocketParameters(const sockaddr_storage& ss, std::string& ip, int& port)
{
  union
  {
    sockaddr_storage ss;
    sockaddr_in      sin;
    sockaddr_in6     sin6;
  } addr;

  addr.ss = ss;

  char ip_str[INET6_ADDRSTRLEN];

  switch (addr.ss.ss_family)
  {
    case AF_INET:
      if (!inet_ntop(AF_INET, &addr.sin.sin_addr, ip_str, sizeof(ip_str)))
      {
        LogosE("inet_ntop(AF_INET) failed");
        return false;
      }
      port = ntohs(addr.sin.sin_port);
      break;

    case AF_INET6:
      if (::isIPv4MappedIPv6(addr.sin6))
      {
        struct in_addr addr4;
#ifdef _WIN32
        memcpy(&addr4.s_addr, &addr.sin6.sin6_addr.u.Byte[12], sizeof(addr4.s_addr));
#else
        memcpy(&addr4.s_addr, &addr.sin6.sin6_addr.s6_addr[12], sizeof(addr4.s_addr));
#endif
        if (!inet_ntop(AF_INET, &addr4, ip_str, sizeof(ip_str)))
        {
          LogosE("inet_ntop(AF_INET) failed");
          return false;
        }
      }
      else
      {
        if (!inet_ntop(AF_INET6, &addr.sin6.sin6_addr, ip_str, sizeof(ip_str)))
        {
          LogosE("inet_ntop(AF_INET6) failed");
          return false;
        }
      }

      port = ntohs(addr.sin6.sin6_port);
      break;

    default:
      LogE("Unsupported address family: %d", addr.ss.ss_family);
      return false;
  }

  ip = ip_str;

  return true;
}

Socket::Socket(SocketPair* pair, int handle, bool enableClose)
  : Pair(pair)
  , CH(Pair->GetChannel())
  , Handle(handle)
  , EnableClose(enableClose)
  , Configured(false)
  , CloseNotify(true)
  , BlockingMode(true)
  , AcceptWouldblock(false)
  , AcceptPort(0)
  , Pid(-1)
  , RxQueue(-1)
  , TxQueue(-1)
#ifdef _WIN32
  , WExitEvent(nullptr)
  , WStopEvent(nullptr)
  , WStopIO(nullptr)
  , WStartTX(nullptr)
#if SKTCOUNTERS
  , Counters{}
#endif
#endif
  , ExitEventCookie(0)
  , CloseEventCookie(0)
  , BreakEventCookie(0)
  , StartTXEventCookie(0)
#if SKTEPOLL
  , Poll(-1)
  , EventDescriptor(-1)
  , EventsMask(0)
  , EpollMask(0)
#endif
  , FailLogged(false)
{
  RxBuffer[0] = '\0';
  StartTX = CreateSynchronizationEvent();

#ifdef _WIN32
  InitWin32Events();
  TxQueue.SetSignallReady(
    std::bind(&Socket::SignallWindowsEvent, this, WStartTX, 0, false)
  );
#endif

#if SKTEPOLL
  TxQueue.SetSignallReady(
    std::bind(&Socket::EventSignalled, this, WAIT_RESULT::OBJECT_4, 0, false)
  );
  
  StartTXEventCookie = StartTX->RegisterWait(
    std::bind(&Socket::EventSignalled, this, WAIT_RESULT::OBJECT_4, _1, _2)
  );

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

#if SKTEPOLL || defined(_WIN32)
  if (BreakRead && BreakEventCookie)
  {
    BreakRead->UnregisterWait(BreakEventCookie);
    BreakEventCookie = 0;
  }
#endif

  CloseHandle(BreakRead);

#ifdef _WIN32
  FreeWin32Events();
#endif

#if SKTEPOLL
  if (StartTXEventCookie)
  {
    StartTX->UnregisterWait(StartTXEventCookie);
    StartTXEventCookie = 0;
  }
#endif

  CloseHandle(StartTX);

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

#ifdef _WIN32
#if SKTCOUNTERS
  std::lock_guard lock(TotalsLock);
  Totals += Counters;
#endif
#endif
}

const IOCounters& Socket::GetTotals()
{
  return Totals;
}

#if defined(_WIN32) && SKTCOUNTERS
IOCountersGroup& Socket::MyCountersGroup()
{
  return Pair->AmIClient(this) ? Counters.Client : Counters.Server;
}
#endif

void Socket::SetLastError(SKT_ERROR e, const char* file, int line)
{
  SocketError error(e, file, line);
  LastError[Syncme::GetCurrentThreadId()] = error;
}

SocketError Socket::GetLastError() const
{
  auto id = Syncme::GetCurrentThreadId();
  auto it = LastError.find(id);
  if (it == LastError.end())
    return SocketError();

  return it->second;
}

std::string Socket::GetProtocol() const
{
  return std::string();
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

#ifdef _WIN32
  if (CloseEventCookie)
  {
    Pair->GetCloseEvent()->UnregisterWait(CloseEventCookie);
    CloseEventCookie = 0;
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
  IOStat stat{};
  IO(0, stat);

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
  else
    EpollMask = ev.events;
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

  // we should not reduce the buffer size 
  // under any circumstances !!!
  int rcvsize = config->GetByteSize("rcvbuf", -1);
  if (rcvsize != -1 && rcvsize > rcvbuf)
  {
    if (setsockopt(Handle, SOL_SOCKET, SO_RCVBUF, (char*)&rcvsize, sizeof(rcvsize)))
    {
      LogosE("setsockopt(SO_RCVBUF) failed");
      return false;
    }

    LogI("%s: new size of rcv buffer is %i", Pair->WhoAmI(this), rcvsize);
  }

  // we should not reduce the buffer size 
  // under any circumstances !!!
  int sndsize = config->GetByteSize("sndbuf", -1);
  if (sndsize != -1 && sndsize > sndbuf)
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
  ULONG delay = (ULONG)config->GetTimeInMilliseconds("keepalive-delay", 15000);
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
#if SKTEPOLL || defined(_WIN32)
    if (BreakEventCookie)
    {
      BreakRead->UnregisterWait(BreakEventCookie);
      BreakEventCookie = 0;
    }
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
    BreakRead = CreateNotificationEvent();
    if (BreakRead == nullptr)
    {
      LogE("CreateCommonEvent failed");
      return false;
    }
#if SKTEPOLL
    BreakEventCookie = BreakRead->RegisterWait(
      std::bind(&Socket::EventSignalled, this, WAIT_RESULT::OBJECT_3, _1, _2)
    );
#elif defined(_WIN32)
    BreakEventCookie = BreakRead->RegisterWait(
      std::bind(&Socket::SignallWindowsEvent, this, WStopIO, _1, _2)
    );
#endif
  }

  if (RxEvent == nullptr)
  {
    RxEvent = CreateSocketEvent(Handle, EVENT_READ | EVENT_WRITE | EVENT_CLOSE, &TxQueue);
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

bool Socket::InitAcceptAddress()
{
  assert(Handle != -1 || Pair->ClosePending);

  if (Handle == -1)
    return false;

  sockaddr_storage ss;
  socklen_t addr_len = sizeof(ss);
  if (getsockname(Handle, reinterpret_cast<sockaddr*>(&ss), &addr_len) == -1)
  {
    LogosE("getsockname failed");
    return false;
  }

  return ::readSocketParameters(ss, AcceptIP, AcceptPort);
}

bool Socket::InitPeer()
{
  assert(Handle != -1 || Pair->ClosePending);

  if (Handle == -1)
    return false;
  
  InitAcceptAddress();

  sockaddr_storage ss;
  socklen_t addr_len = sizeof(ss);
  if (getpeername(Handle, reinterpret_cast<sockaddr*>(&ss), &addr_len) == -1)
  {
    LogosE("getpeername failed");
    return false;
  }

  return ::readSocketParameters(ss, Peer.IP, Peer.Port);
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

  if (inet_addr(name) != INADDR_NONE)
  {
    Peer.IP = name;
    Peer.Port = port;
    return true;
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

#ifdef USE_LOGME
void Socket::DumpGroup(
  const Logme::ID& CH
  , Logme::Override& ovr
  , const IOCountersGroup& g
  , const char* title
)
{
  LogmeI(CH, ovr, "%s:", title);
  LogmeI(CH, ovr, "IOFunctionCall: %i", (int)g.IOFunctionCall);
  LogmeI(CH, ovr, "TotalIOLoops: %i", (int)g.TotalIOLoops);
  LogmeI(CH, ovr, "AverageLoopTime: %i", g.TotalIOLoops ? int(g.TotalLoopTime / g.TotalIOLoops) : 0);
  LogmeI(CH, ovr, "AverageWaitTime: %i", g.WaitCount? int(g.TotalWaitTime / g.WaitCount) : 0);
  LogmeI(CH, ovr, "Timeouts: %i", (int)g.Timeouts);

  for (size_t i = 0; i < sizeof(g.Events) / sizeof(g.Events[0]); ++i)
    LogmeI(CH, ovr, "%s: %i", EvName[i], (int)g.Events[i]);

  LogmeI(CH, ovr, "");
}

void Socket::DumpTotals(const Logme::ID& CH)
{
#if SKTCOUNTERS
  std::lock_guard lock(TotalsLock);

  Logme::Override ovr;
  ovr.Remove.Method = true;
  LogmeI(CH, ovr, "[SOCKET TOTALS]");
  LogmeI(CH, ovr, "---------------");
  DumpGroup(CH, ovr, Totals.Client, "[Client]");
  DumpGroup(CH, ovr, Totals.Server, "[Server]");
#endif
}
#endif

bool Socket::IsIPv6(const std::string& ip)
{
  return IsIPv6(ip.c_str());
}

bool Socket::IsIPv6(const char* ip)
{
  struct in6_addr addr6{};
  return inet_pton(AF_INET6, ip, &addr6) == 1;
}

bool Socket::IsIPv4MappedIPv6(const std::string& ip)
{
  return IsIPv4MappedIPv6(ip.c_str());
}

bool Socket::IsIPv4MappedIPv6(const char* ip)
{
  if (!ip)
    return false;
    
  struct sockaddr_in6 saddr6;
  memset(&saddr6, 0, sizeof(saddr6));
  saddr6.sin6_family = AF_INET6;

  if (inet_pton(AF_INET6, ip, &saddr6.sin6_addr) != 1)
    return false;

  return ::isIPv4MappedIPv6(saddr6);
}

bool Socket::IsLoopbackIP(const std::string& ip)
{
  return IsLoopbackIP(ip.c_str());
}

bool Socket::IsLoopbackIP(const char* ip)
{
  struct in_addr ipv4{};
  if (inet_pton(AF_INET, ip, &ipv4) == 1)
  {
#ifdef WIN32
    return (ipv4.S_un.S_un_b.s_b1 == 127);
#else
    return ((ntohl(ipv4.s_addr) >> 24) & 0xFF) == 127;
#endif
  }

  struct in6_addr ipv6{};
  if (inet_pton(AF_INET6, ip, &ipv6) == 1)
  {
    const uint8_t loopback6[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 };
    return (memcmp(&ipv6, loopback6, 16) == 0);
  }

  return false;
}

bool Socket::ReadSocketParameters(const sockaddr_storage& ss, std::string& ip, int& port)
{
  return ::readSocketParameters(ss, ip, port);
}
