#pragma once

#include <mutex>

#include <openssl/ssl.h>

#include <Syncme/Api.h>
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
    SINCMELNK SocketPair(CHANNEL& ch, HEvent exitEvent, ConfigPtr config);
    SINCMELNK ~SocketPair();

    SINCMELNK void Close();
    SINCMELNK bool Closing() const;
    SINCMELNK int PeerDisconnected();

    SINCMELNK HEvent GetExitEvent() const;
    SINCMELNK HEvent GetCloseEvent() const;

    SINCMELNK CHANNEL& GetChannel();
    SINCMELNK ConfigPtr GetConfig();

    SINCMELNK SocketPtr CreateBIOSocket();
    SINCMELNK SocketPtr CreateSSLSocket(SSL* ssl);

    SINCMELNK const char* WhoAmI(SocketPtr socket) const;
    SINCMELNK const char* WhoAmI(Socket* socket) const;

    SINCMELNK int Read(std::vector<char>& buffer, SocketPtr& from, int timeout = FOREVER);
    SINCMELNK int Read(void* buffer, size_t size, SocketPtr& from, int timeout = FOREVER);

  private:
    int IO(SocketPtr socket, void* buffer, size_t size, SocketPtr& from, int timeout);
  };
}