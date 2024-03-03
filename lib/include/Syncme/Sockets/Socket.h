#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

    ConnectedPeer() : Port(0), Disconnected(false), When(0)
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
    Socket(SocketPair* pair, int handle = -1, bool enableClose = true);
    virtual ~Socket();

    virtual int Detach(bool* enableClose = nullptr);
    virtual bool Attach(int socket, bool enableClose = true);
    virtual void Close();
    virtual bool Configure();
    virtual bool StopPendingRead();
    virtual void Shutdown() = 0;
    virtual void Unread(const char* p, size_t n);
    
    bool IsAttached() const;
    bool PeerDisconnected() const;
    bool ReadPeerDisconnected();

    int Read(std::vector<char>& buffer, int timeout = FOREVER);
    virtual int Read(void* buffer, size_t size, int timeout = FOREVER) = 0;

    int WriteStr(const std::string& str, int timeout = FOREVER);
    int Write(const std::vector<char>& arr, int timeout = FOREVER);
    int Write(const void* buffer, size_t size, int timeout = FOREVER);

    virtual int GetFD() const = 0;
    virtual SKT_ERROR Ossl2SktError(int ret) const = 0;
    virtual void LogIoError(const char* fn, const char* text) = 0;

    bool InitPeer();
    bool PeerFromHostString(const std::string& host, const std::string& scheme);

    void AddLimit(int code, size_t count, uint64_t duration);
    bool ReportError(int code);

    void SetLastError(SKT_ERROR e, const char* file, int line);
    const SocketError& GetLastError() const;

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
