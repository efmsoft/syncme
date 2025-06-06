#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sleep.h>
#include <Syncme/Sockets/BIOSocket.h>
#include <Syncme/Sockets/SocketEvent.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/Sockets/SSLSocket.h>
#include <Syncme/TickCount.h>

using namespace Syncme;
using namespace Syncme::Implementation;

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

bool SocketPair::IsDisconnected()
{
  std::lock_guard<std::mutex> lock(CloseLock);

  if (Client && Client->PeerDisconnected())
    return true;

  if (Server && Server->PeerDisconnected())
    return true;

  return false;
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

bool SocketPair::AmIClient(Socket* socket) const
{
  return socket == Client.get();
}

bool SocketPair::AmIServer(Socket* socket) const
{
  return socket == Server.get();
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

void SocketPair::ResetPendingRead()
{
  if (Client)
    Client->ResetPendingRead();

  if (Server)
    Server->ResetPendingRead();
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
    if (b->size() > size)
    {
      assert(!"buffer size have to be Sockets::IO::BUFFER_SIZE or greater");
      return -1;
    }

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

#ifdef _WIN32
  HANDLE object[4]{};
  object[0] = Server->WBreakWait;
  object[1] = Client->WBreakWait;
  object[2] = ((SocketEvent*)Server->RxEvent.get())->GetWSAEvent();
  object[3] = ((SocketEvent*)Client->RxEvent.get())->GetWSAEvent();
#endif

  for (int loops = 0, zeroCnt = 0;; ++loops)
  {
    n = IO(Client, buffer, size, from, 0);
    if (n != 0)
    {
      return n;
    }

    n = IO(Server, buffer, size, from, 0);
    if (n != 0)
    {
      return n;
    }
    zeroCnt++;

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

#ifdef _WIN32
    WAIT_RESULT rc{};
    auto wr = ::WaitForMultipleObjects(
      4
      , object
      , false
      , milliseconds
    );

    switch (wr)
    {
    case WAIT_TIMEOUT: rc = WAIT_RESULT::TIMEOUT; break;

    case WAIT_OBJECT_0: 
      if (GetEventState(GetExitEvent()) == STATE::SIGNALLED)
      {
        rc = WAIT_RESULT::OBJECT_0;
        break;
      }
      else if (GetEventState(GetCloseEvent()) == STATE::SIGNALLED)
      {
        rc = WAIT_RESULT::OBJECT_1;
        break;
      }
      else
        rc = WAIT_RESULT::OBJECT_3;

      break;

    case WAIT_OBJECT_0 + 1:
      if (GetEventState(GetExitEvent()) == STATE::SIGNALLED)
      {
        rc = WAIT_RESULT::OBJECT_0;
        break;
      }
      else if (GetEventState(GetCloseEvent()) == STATE::SIGNALLED)
      {
        rc = WAIT_RESULT::OBJECT_1;
        break;
      }
      else 
        rc = WAIT_RESULT::OBJECT_5;

      break;

    case WAIT_OBJECT_0 + 2: rc = WAIT_RESULT::OBJECT_2; break;
    case WAIT_OBJECT_0 + 3: rc = WAIT_RESULT::OBJECT_4; break;
    
    case WAIT_FAILED:
    default:
      rc = WAIT_RESULT::FAILED; break;
    }
#else
    auto rc = WaitForMultipleObjects(events, false, milliseconds);
#endif

    if (rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1)
    {
      Server->SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      Client->SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      return -1;
    }

    if (rc == WAIT_RESULT::OBJECT_3)
    {
      Server->ResetPendingRead();

      LogI("Break server read");
      Server->SKT_SET_LAST_ERROR(NONE);
      return 0;
    }

    if (rc == WAIT_RESULT::OBJECT_5)
    {
      Client->ResetPendingRead();

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
      zeroCnt = 0;
      continue;
    }

    if (rc == WAIT_RESULT::FAILED)
    {
      Server->SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      Client->SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      return -1;
    }

    SocketPtr socket = rc == WAIT_RESULT::OBJECT_2 ? Server : Client;

    int netev = 0;

#ifdef _WIN32
    WSANETWORKEVENTS nev{};
    int stat = WSAEnumNetworkEvents(
      socket->Handle
      , ((SocketEvent*)socket->RxEvent.get())->GetWSAEvent()
      , &nev
    );

    if (stat)
    {
      socket->SKT_SET_LAST_ERROR(GENERIC);
      return false;
    }

    if (nev.lNetworkEvents & FD_CLOSE)
      netev |= EVENT_CLOSE;

    if (nev.lNetworkEvents & FD_WRITE)
      netev |= EVENT_WRITE;

    if (nev.lNetworkEvents & FD_READ)
      netev |= EVENT_READ;
#else
    netev = GetSocketEvents(socket->RxEvent);
#endif
    if (netev & EVENT_CLOSE)
    {
      socket->Peer.Disconnected = true;
      socket->Peer.When = GetTimeInMillisec();
      timeout = 0;

      LogW("%s: peer disconnected", WhoAmI(socket));

      // We have to drain input buffer before closing socket
    }
    // Workaround strange pattern - SSLread returns 0, but SSL_ERROR_ZERO_RETURN is not set.
    // Here we just slow down requests to avoid high CPU load produced ininite empty cycles.
    // IO will be stopped after some timeout, typically 30 sec.
    if (zeroCnt >= 5)
    {
      // Send a couple messages to log to indicate the problem
      if (zeroCnt == 5 || zeroCnt == 6)
      {
        LogI("%s: fd=%d start 'sleep' mode, zero 'read' counter %d, rc %d", WhoAmI(socket), socket->Handle, zeroCnt, rc);
      }
      // Disconnect
      if (zeroCnt > 8)
      {
        LogW("%s: fd=%d long 'sleep' detected, counter %d, rc %d, closing socket", WhoAmI(socket), socket->Handle, zeroCnt, rc);
        socket->Peer.Disconnected = true;
        socket->Peer.When = GetTimeInMillisec();
        timeout = 0;
      }

      Sleep(50);
    }
  }

  Server->SKT_SET_LAST_ERROR(NONE);
  Client->SKT_SET_LAST_ERROR(NONE);
  return n;
}
