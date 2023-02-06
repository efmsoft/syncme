#include <cassert>

#include <Syncme/TickCount.h>
#include <Syncme/Sockets/BIOSocket.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/Sockets/SSLSocket.h>

using namespace Syncme;

const uint64_t PEER_DISCONNECT_TIMEOUT = 2000;

SocketPair::SocketPair(CHANNEL& ch, HEvent exitEvent, ConfigPtr config)
  : CH(ch)
  , ExitEvent(exitEvent)
  , Config(config)
  , CloseEvent(nullptr)
  , ClosePending(false)
  , PeerDisconnect(0)
{
  CloseEvent = CreateNotificationEvent();
}

SocketPair::~SocketPair()
{
  if (CloseEvent)
    CloseHandle(CloseEvent);
}

ConfigPtr SocketPair::GetConfig()
{
  return Config;
}

HEvent SocketPair::GetExitEvent() const
{
  return ExitEvent;
}

HEvent SocketPair::GetCloseEvent() const
{
  return CloseEvent;
}

CHANNEL& SocketPair::GetChannel()
{
  return CH;
}

bool SocketPair::Closing() const
{
  if (ClosePending)
    return true;

  if (PeerDisconnect == 0)
    return false;

  auto t = GetTimeInMillisec() - PeerDisconnect;
  if (t > PEER_DISCONNECT_TIMEOUT)
    return true;

  return false;
}

int SocketPair::PeerDisconnected()
{
  std::lock_guard<std::mutex> lock(CloseLock);

  if (PeerDisconnect == 0)
    PeerDisconnect = GetTimeInMillisec();

  return int(PEER_DISCONNECT_TIMEOUT);
}

void SocketPair::Close()
{
  std::lock_guard<std::mutex> lock(CloseLock);

  if (ClosePending)
    return;

  LogI("Closing socket pair");

  if (Client)
  {
    LogI("Shutting down client");
    Client->Shutdown();
  }

  if (Server)
  {
    LogI("Shutting down server");
    Server->Shutdown();
  }

  ClosePending = true;
  SetEvent(CloseEvent);

  if (Client)
  {
    LogI("Closing client socket");
    Client->Close();
  }

  if (Server)
  {
    LogI("Closing server socket");
    Server->Close();
  }
}

SocketPtr SocketPair::CreateBIOSocket()
{
  return std::make_shared<BIOSocket>(this);
}

SocketPtr SocketPair::CreateSSLSocket(SSL* ssl)
{
  return std::make_shared<SSLSocket>(this, ssl);
}

const char* SocketPair::WhoAmI(Socket* socket) const
{
  if (socket == Client.get())
    return "client";

  if (socket == Server.get())
    return "server";

  return "nobody";
}