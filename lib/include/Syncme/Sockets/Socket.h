#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <Syncme/Logger/Channel.h>
#include <Syncme/Sockets/ErrorLimit.h>
#include <Syncme/Sync.h>

namespace Syncme
{
  struct SocketPair;

  enum class SKT_ERROR  
  {
    NONE,

    TIMEOUT,
    GRACEFUL_DISCONNECT,
    WOULDBLOCK,
    IO_INCOMPLETE,
    CONNECTION_ABORTED,
    GENERIC,
  };

  struct ConnectedPeer
  {
    std::string IP;
    int Port;
    bool Disconnected;

    ConnectedPeer() : Port(0), Disconnected(false)
    {
    }
  };

  struct Socket
  {
    SocketPair* Pair;
    CHANNEL& CH;

    std::mutex Lock;
    int Handle;
    bool EnableClose;
    HEvent RxEvent;
    HEvent BreakRead;

    ConnectedPeer Peer;
    bool Configured;
    bool CloseNotify;
    bool BlockingMode;

    SKT_ERROR LastError;
    std::map<int, ErrorLimitPtr> Limits;

  public:
    Socket(SocketPair* pair, int handle = -1, bool enableClose = true);
    virtual ~Socket();

    virtual int Detach(bool* enableClose = nullptr);
    virtual bool Attach(int socket, bool enableClose = true);
    virtual void Close();
    virtual bool Configure();
    virtual bool StopPendingRead();
    virtual void Shutdown() = 0;
    
    bool IsAttached() const;
    bool PeerDisconnected() const;

    int Read(std::vector<char>& buffer, int timeout = FOREVER);
    virtual int Read(void* buffer, size_t size, int timeout = FOREVER) = 0;

    int WriteStr(const std::string& str, int timeout = FOREVER);
    int Write(const std::vector<char>& arr, int timeout = FOREVER);
    int Write(const void* buffer, size_t size, int timeout = FOREVER);

    virtual int GetFD() const = 0;
    virtual SKT_ERROR GetError(int ret) const = 0;
    virtual void LogIoError(const char* fn, const char* text) = 0;

    bool InitPeer();
    bool PeerFromHostString(const std::string& host, const std::string& scheme);

    void AddLimit(int code, size_t count, uint64_t duration);
    bool ReportError(int code);

    void SetLastError(SKT_ERROR e);
    SKT_ERROR GetLastError() const;
    std::string GetLastErrorStr() const;

  protected:
    virtual bool SetOptions();
    virtual bool SwitchToUnblockingMode();

    int WaitRxReady(int timeout);
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

