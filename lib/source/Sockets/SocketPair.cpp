#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/BIOSocket.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/Sockets/SSLSocket.h>
#include <Syncme/TickCount.h>

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
  Close();
  
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
    Client->Flush();
    Client->Shutdown();
  }

  if (Server)
  {
    LogI("Shutting down server");
    Server->Flush();
    Server->Shutdown();
  }

  ClosePending = true;
  SetEvent(CloseEvent);

  auto client = Client;
  if (client)
  {
    LogI("Closing client socket");
    client->Close();
  }

  auto server = Server;
  if (server)
  {
    LogI("Closing server socket");
    server->Close();
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

const char* SocketPair::WhoAmI(SocketPtr socket) const
{
  return WhoAmI(socket.get());
}

const char* SocketPair::WhoAmI(Socket* socket) const
{
  if (socket == Client.get())
    return "client";

  if (socket == Server.get())
    return "server";

  return "nobody";
}

int SocketPair::Read(std::vector<char>& buffer, SocketPtr& from, int timeout)
{
  return Read(&buffer[0], buffer.size(), from, timeout);
}


int SocketPair::IO(
  SocketPtr socket
  , void* buffer
  , size_t size
  , SocketPtr& from
  , int timeout
)
{
  IOStat io{};
  if (socket->IO(timeout, io) == false)
  {
    from = socket;
    return -1;
  }

  auto b = socket->RxQueue.Join(size);
  if (b != nullptr)
  {
    memcpy(buffer, b->data(), b->size());
    int n = int(b->size());

    socket->RxQueue.PushFree(b);
    from = socket;
    return n;
  }

  return 0;
}

int SocketPair::Read(void* buffer, size_t size, SocketPtr& from, int timeout)
{
  auto start = GetTimeInMillisec();

  int n = 0;
  IOStat io{};
  from.reset();

  auto serverValid = Server != nullptr && Server->RxEvent != nullptr;
  auto clientValid = Client != nullptr && Client->RxEvent != nullptr;

  if (!serverValid && !clientValid)
    return -1;

  if (clientValid && !serverValid)
    return IO(Client, buffer, size, from, timeout);

  if (serverValid && !clientValid)
    return IO(Server, buffer, size, from, timeout);
 
  EventArray events(
    GetExitEvent()
    , GetCloseEvent()
    , Server->RxEvent
    , Server->BreakRead
    , Client->RxEvent
    , Client->BreakRead
  );

  for (int loops = 0;; ++loops)
  {
    n = IO(Client, buffer, size, from, 0);
    if (n != 0)
      return n;

    n = IO(Server, buffer, size, from, 0);
    if (n != 0)
      return n;

    if (Client->Peer.Disconnected || Server->Peer.Disconnected)
    {
      Server->SKT_SET_LAST_ERROR(GRACEFUL_DISCONNECT);
      Client->SKT_SET_LAST_ERROR(GRACEFUL_DISCONNECT);
      return -1;
    }

    auto t = GetTimeInMillisec();
    uint32_t milliseconds = FOREVER;

    if (timeout != FOREVER)
    {
      if (t - start >= timeout)
      {
        Server->SKT_SET_LAST_ERROR2(Server->Peer.Disconnected ? SKT_ERROR::GRACEFUL_DISCONNECT : SKT_ERROR::TIMEOUT);
        Client->SKT_SET_LAST_ERROR2(Client->Peer.Disconnected ? SKT_ERROR::GRACEFUL_DISCONNECT : SKT_ERROR::TIMEOUT);
        return 0;
      }

      milliseconds = uint32_t(start + timeout - t);
    }

    auto rc = WaitForMultipleObjects(events, false, milliseconds);
    if (rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1)
    {
      Server->SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      Client->SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      return -1;
    }

    if (rc == WAIT_RESULT::OBJECT_3)
    {
      LogI("Break server read");
      Server->SKT_SET_LAST_ERROR(NONE);
      return 0;
    }

    if (rc == WAIT_RESULT::OBJECT_5)
    {
      LogI("Break client read");
      Client->SKT_SET_LAST_ERROR(NONE);
      return 0;
    }

    if (rc == WAIT_RESULT::TIMEOUT)
    {
      t = GetTimeInMillisec();
      if (t - start >= timeout)
      {
        Server->SKT_SET_LAST_ERROR2(Server->Peer.Disconnected ? SKT_ERROR::GRACEFUL_DISCONNECT : SKT_ERROR::TIMEOUT);
        Client->SKT_SET_LAST_ERROR2(Client->Peer.Disconnected ? SKT_ERROR::GRACEFUL_DISCONNECT : SKT_ERROR::TIMEOUT);
        return 0;
      }

      continue;
    }

    if (rc == WAIT_RESULT::FAILED)
    {
      Server->SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      Client->SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      return -1;
    }

    SocketPtr socket = rc == WAIT_RESULT::OBJECT_2 ? Server : Client;

    int netev = GetSocketEvents(socket->RxEvent);
    if (netev & EVENT_CLOSE)
    {
      socket->Peer.Disconnected = true;
      socket->Peer.When = GetTimeInMillisec();
      timeout = 0;

      LogW("%s: peer disconnected", WhoAmI(socket));

      // We have to drain input buffer before closing socket
    }
  }

  Server->SKT_SET_LAST_ERROR(NONE);
  Client->SKT_SET_LAST_ERROR(NONE);
  return n;
}
