#pragma once

#include <stdint.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

#include <Syncme/Api.h>
#include <Syncme/CritSection.h>
#include <Syncme/Logger/Channel.h>
#include <Syncme/Sockets/ErrorLimit.h>
#include <Syncme/Sockets/SocketError.h>
#include <Syncme/Sockets/Queue.h>
#include <Syncme/Sync.h>

#if !defined(_WIN32)
#define SKTEPOLL 1
#else
#define SKTEPOLL 0
#endif

#define SKTIODEBUG 0
#define SKTCOUNTERS 0

struct sockaddr_storage;
struct sockaddr_in6;

namespace Syncme
{
  struct SocketPair;

  struct ConnectedPeer
  {
    std::string IP;
    int Port;

    bool Disconnected;
    uint64_t When;

    SINCMELNK ConnectedPeer() : Port(0), Disconnected(false), When(0)
    {
    }
  };

  struct IOStat
  {
    size_t Sent;
    size_t SentPkt;
    uint64_t SendTime;

    size_t Rcv;
    size_t RcvPkt;
    uint64_t RcvTime;

    size_t Wait;
    uint64_t WaitTime;

    uint64_t Cycles;

#if SKTIODEBUG
    char History[4096];
#endif
  };

  struct IOCountersGroup
  {
    uint32_t IOFunctionCall; // Number of IO() calls
    uint32_t TotalIOLoops; // Total number of IO() loops
    uint32_t TotalLoopTime; // Total time of IO() loops
    uint32_t TotalWaitTime; // Total time of WaitForMultipleObjects calls
    uint32_t WaitCount; // Number of WaitForMultipleObjects calls with time < 100ms
    uint32_t Timeouts; // Number of timeouts
    uint32_t Events[5]; // Number of events

    IOCountersGroup& operator+=(const IOCountersGroup& g)
    {
      IOFunctionCall += g.IOFunctionCall;
      TotalIOLoops += g.TotalIOLoops;
      TotalLoopTime += g.TotalLoopTime;
      TotalWaitTime += g.TotalWaitTime;
      WaitCount += g.WaitCount;

      Timeouts += g.Timeouts;

      for (size_t i = 0; i < sizeof(Events) / sizeof(Events[0]); ++i)
        Events[i] += g.Events[i];

      return *this;
    }
  };

  struct IOCounters
  {
    IOCountersGroup Server;
    IOCountersGroup Client;
  
    IOCounters& operator+=(const IOCounters& c)
    {
      Server += c.Server;
      Client += c.Client;
      return *this;
    }
  };

  union IOFlags
  {
    uint32_t All;
    struct
    {
      uint32_t Flush : 1;
      uint32_t ForceWait : 1;
      uint32_t : 30;
    } f;
  };

  struct Socket
  {
    SocketPair* Pair;
    CHANNEL& CH;

    CS Lock;
    int Handle;
    bool EnableClose;
    HEvent RxEvent;
    HEvent BreakRead;
    HEvent StartTX;

    ConnectedPeer Peer;
    bool Configured;
    bool CloseNotify;
    bool BlockingMode;
    bool AcceptWouldblock;

    std::string AcceptIP;
    int AcceptPort;

    std::mutex ErrorLock;
    std::unordered_map<uint64_t, SocketError> LastError;

    typedef std::vector<char> Packet;
    typedef std::shared_ptr<Packet> PacketPtr;
    typedef std::list<PacketPtr> PacketQueue;
    PacketQueue Packets;

    int Pid;

    Sockets::IO::Queue RxQueue;
    Sockets::IO::Queue TxQueue;
    std::mutex IOLock;
    std::mutex TxLock;

    bool FailLogged;

#ifdef _WIN32
    void* WBreakWait;

  #if SKTCOUNTERS
    IOCounters Counters;
  #endif
#endif
    static std::mutex TotalsLock;
    static IOCounters Totals;

    uint32_t ExitEventCookie;
    uint32_t CloseEventCookie;
    uint32_t BreakEventCookie;
    uint32_t StartTXEventCookie;

#if SKTEPOLL
    int Poll;
    int EventDescriptor;
    int EventsMask;
    uint32_t EpollMask;
#endif

    char RxBuffer[Sockets::IO::BUFFER_SIZE];

    enum WaitOrder
    {
      evSocket,
      evTX,
      evBreak,
      evExit,
      evStop
    };

    static const char* EvName[5];

  public:
    SINCMELNK Socket(SocketPair* pair, int handle = -1, bool enableClose = true);
    SINCMELNK virtual ~Socket();

    SINCMELNK virtual int Detach(bool* enableClose = nullptr);
    SINCMELNK virtual bool Attach(int socket, bool enableClose = true);
    SINCMELNK virtual void Close();
    SINCMELNK virtual bool Configure();
    SINCMELNK virtual bool StopPendingRead();
    SINCMELNK virtual bool ResetPendingRead();
    SINCMELNK virtual void Shutdown() = 0;
    SINCMELNK virtual void Unread(const char* p, size_t n);
    
    SINCMELNK bool IsAttached() const;
    SINCMELNK bool PeerDisconnected() const;
    SINCMELNK bool ReadPeerDisconnected();

    SINCMELNK int Read(std::vector<char>& buffer, int timeout = FOREVER);
    SINCMELNK int Read(void* buffer, size_t size, int timeout = FOREVER);

    SINCMELNK int WriteStr(const std::string& str, int timeout = FOREVER, bool* queued = nullptr);
    SINCMELNK int Write(const std::vector<char>& arr, int timeout = FOREVER, bool* queued = nullptr);
    SINCMELNK int Write(const void* buffer, size_t size, int timeout = FOREVER, bool* queued = nullptr);

    SINCMELNK virtual int GetFD() const = 0;
    SINCMELNK virtual SKT_ERROR Ossl2SktError(int ret) = 0;
    SINCMELNK virtual void LogIoError(const char* fn, const char* text) = 0;
    SINCMELNK virtual std::string GetProtocol() const;

    SINCMELNK bool InitPeer();
    SINCMELNK bool InitAcceptAddress();
    SINCMELNK bool PeerFromHostString(
      const std::string& host
      , const std::string& scheme
      , int af
      , int type
    );

    SINCMELNK void SetLastError(SKT_ERROR e, const char* file, int line);
    SINCMELNK SocketError GetLastError();

    SINCMELNK virtual bool SetOptions();
    SINCMELNK virtual bool SwitchToUnblockingMode();
    SINCMELNK virtual bool SwitchToBlockingMode();

    SINCMELNK virtual int InternalRead(void* buffer, size_t size, int timeout) = 0;

    SINCMELNK virtual bool IO(int timeout, IOStat& stat, IOFlags flags = IOFlags{});
    SINCMELNK virtual bool Flush(int timeout = -1);

    SINCMELNK static const IOCounters& GetTotals();
    
    SINCMELNK static bool IsLoopbackIP(const char* ip);
    SINCMELNK static bool IsLoopbackIP(const std::string& ip);

    SINCMELNK static bool IsIPv6(const char* ip);
    SINCMELNK static bool IsIPv6(const std::string& ip);

    SINCMELNK static bool IsIPv4MappedIPv6(const char* ip);
    SINCMELNK static bool IsIPv4MappedIPv6(const sockaddr_in6& saddr6);
    SINCMELNK static bool IsIPv4MappedIPv6(const std::string& ip);

    SINCMELNK static bool ReadSocketParameters(const sockaddr_storage& ss, std::string& ip, int& port);

#ifdef USE_LOGME
    SINCMELNK static void DumpTotals(const Logme::ID& CH);
#endif

  protected:
    bool ReadIO(IOStat& stat);
    bool WriteIO(IOStat& stat);

#if SKTEPOLL
    WAIT_RESULT FastWaitForMultipleObjects(int timeout, IOStat& stat);
    WAIT_RESULT EventStateToWaitResult();
    bool UpdateEpollEventList();
#endif

#if SKTEPOLL
    void EventSignalled(WAIT_RESULT r, uint32_t cookie, bool failed);
    void ResetEventObject(); 
#endif

#ifdef _WIN32
    void InitWin32Events();
    void FreeWin32Events();
    void SignallWindowsEvent(void* h, uint32_t cookie, bool failed);
    bool ReadSocketEvents(IOStat& stat, void* h);
    void EventSelect(IOStat& stat);
#endif

#if SKTIODEBUG
    void IoDebug(IOStat& stat, const char* op, int n, const char* tn = nullptr);
#endif

    int ReadPacket(void* buffer, size_t size);
    static uint32_t CalculateTimeout(int timeout, uint64_t start, bool& expired);

    virtual int InternalWrite(const void* buffer, size_t size, int timeout) = 0;

#if defined(_WIN32) && SKTCOUNTERS
    IOCountersGroup& MyCountersGroup();
#endif

    static void DumpGroup(
      const Logme::ID& CH
      , Logme::Override& ovr
      , const IOCountersGroup& g
      , const char* title
    );
  };

  typedef std::shared_ptr<Socket> SocketPtr;
}

#define RX_TIMEOUT(n) \
  ((n) == 0 && GetLastError() == SKT_ERROR::TIMEOUT) 

#define RX_DISCONNECT(n) \
  ((n) == 0 && GetLastError() == SKT_ERROR::GRACEFUL_DISCONNECT) 

#define RX_ZERO_RETURN(n) \
  ((n) == 0 && (GetLastError() == SKT_ERROR::TIMEOUT || GetLastError() == SKT_ERROR::NONE)) 

#define SKT_SET_LAST_ERROR(e) \
  SetLastError(SKT_ERROR::e, __FILE__, __LINE__) 

#define SKT_SET_LAST_ERROR2(e) \
  SetLastError(e, __FILE__, __LINE__) 

#if SKTIODEBUG
#define IODEBUG(op, n) IoDebug(stat, op, int(n))
#define IODEBUGS(op, tn) IoDebug(stat, op, 0, tn)
#else
#define IODEBUG(...)
#define IODEBUGS(...)
#endif

#if defined(_WIN32) && SKTCOUNTERS
#define SKTCOUNTERADD(n, v) MyCountersGroup().##n += v 
#else
#define SKTCOUNTERADD(n, v)
#endif
