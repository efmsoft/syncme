#pragma once

#include <mutex>

#include <openssl/ssl.h>

#include <Syncme/Config/Config.h>
#include <Syncme/Logger/Channel.h>
#include <Syncme/Sockets/Socket.h>
#include <Syncme/Sync.h>
#include <Syncme/TimePoint.h>

namespace Syncme
{
  constexpr const size_t BUFFER_SIZE = 64 * 1024; // 64K
  constexpr const int READ_REQUEST_TIMEOUT = 30000;

  struct SocketPair
  {
    CHANNEL& CH;
    HEvent ExitEvent;
    ConfigPtr Config;

    std::mutex CloseLock;
    HEvent CloseEvent;
    bool ClosePending;

    SocketPtr Client;
    SocketPtr Server;

    uint64_t PeerDisconnect;

  public:
    SocketPair(CHANNEL& ch, HEvent exitEvent, ConfigPtr config);
    ~SocketPair();

    void Close();
    bool Closing() const;
    int PeerDisconnected();

    HEvent GetExitEvent() const;
    HEvent GetCloseEvent() const;

    CHANNEL& GetChannel();
    ConfigPtr GetConfig();

    SocketPtr CreateBIOSocket();
    SocketPtr CreateSSLSocket(SSL* ssl);

    const char* WhoAmI(Socket* socket) const;
  };
}