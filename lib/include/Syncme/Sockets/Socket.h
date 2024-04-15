#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <Syncme/Api.h>
#include <Syncme/CritSection.h>
#include <Syncme/Logger/Channel.h>
#include <Syncme/Sockets/ErrorLimit.h>
#include <Syncme/Sockets/SocketError.h>
#include <Syncme/Sync.h>

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

  struct Socket
  {
    SocketPair* Pair;
    CHANNEL& CH;

    CS Lock;
    int Handle;
    bool EnableClose;
    HEvent RxEvent;
    HEvent BreakRead;

    ConnectedPeer Peer;
    bool Configured;
    bool CloseNotify;
    bool BlockingMode;

    SocketError LastError;
    std::map<int, ErrorLimitPtr> Limits;

    typedef std::vector<char> Packet;
    typedef std::shared_ptr<Packet> PacketPtr;
    typedef std::list<PacketPtr> PacketQueue;
    PacketQueue Packets;

    int Pid;

  public:
    SINCMELNK Socket(SocketPair* pair, int handle = -1, bool enableClose = true);
    SINCMELNK virtual ~Socket();

    SINCMELNK virtual int Detach(bool* enableClose = nullptr);
    SINCMELNK virtual bool Attach(int socket, bool enableClose = true);
    SINCMELNK virtual void Close();
    SINCMELNK virtual bool Configure();
    SINCMELNK virtual bool StopPendingRead();
    SINCMELNK virtual void Shutdown() = 0;
    SINCMELNK virtual void Unread(const char* p, size_t n);
    
    SINCMELNK bool IsAttached() const;
    SINCMELNK bool PeerDisconnected() const;
    SINCMELNK bool ReadPeerDisconnected();

    SINCMELNK int Read(std::vector<char>& buffer, int timeout = FOREVER);
    SINCMELNK virtual int Read(void* buffer, size_t size, int timeout = FOREVER) = 0;

    SINCMELNK int WriteStr(const std::string& str, int timeout = FOREVER);
    SINCMELNK int Write(const std::vector<char>& arr, int timeout = FOREVER);
    SINCMELNK int Write(const void* buffer, size_t size, int timeout = FOREVER);

    SINCMELNK virtual int GetFD() const = 0;
    SINCMELNK virtual SKT_ERROR Ossl2SktError(int ret) const = 0;
    SINCMELNK virtual void LogIoError(const char* fn, const char* text) = 0;

    SINCMELNK bool InitPeer();
    SINCMELNK bool PeerFromHostString(
      const std::string& host
      , const std::string& scheme
      , int af
      , int type
    );

    SINCMELNK void AddLimit(int code, size_t count, uint64_t duration);
    SINCMELNK bool ReportError(int code);

    SINCMELNK void SetLastError(SKT_ERROR e, const char* file, int line);
    SINCMELNK const SocketError& GetLastError() const;

  protected:
    virtual bool SetOptions();
    virtual bool SwitchToUnblockingMode();

    int WaitRxReady(int timeout);
    int ReadPacket(void* buffer, size_t size);
    virtual int InternalWrite(const void* buffer, size_t size, int timeout) = 0;
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
