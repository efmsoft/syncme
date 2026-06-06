#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/SocketEventLoop.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/Sync.h>

using namespace Syncme;

namespace
{
  constexpr const char DATA[] = "socket event loop data";
  constexpr const char CLIENT_TO_SERVER[] = "raw client to server payload";
  constexpr const char SERVER_TO_CLIENT[] = "raw server to client payload";

  static void CloseSocketHandle(int handle)
  {
    if (handle != -1)
      closesocket(handle);
  }

  static bool SetReceiveTimeout(int handle, int timeout)
  {
#ifdef _WIN32
    DWORD value = DWORD(timeout);
#else
    timeval value{};
    value.tv_sec = timeout / 1000;
    value.tv_usec = (timeout % 1000) * 1000;
#endif

    return setsockopt(
      handle
      , SOL_SOCKET
      , SO_RCVTIMEO
      , (const char*)&value
      , sizeof(value)
    ) == 0;
  }

  static bool CreateListenSocket(int& handle, int& port)
  {
    handle = int(socket(AF_INET, SOCK_STREAM, 0));
    if (handle == -1)
    {
      LogosE("socket() failed");
      return false;
    }

    int reuse = 1;
    if (setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
    {
      LogosE("setsockopt(SO_REUSEADDR) failed");
      CloseSocketHandle(handle);
      handle = -1;
      return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(handle, (sockaddr*)&addr, sizeof(addr)) == -1)
    {
      LogosE("bind() failed");
      CloseSocketHandle(handle);
      handle = -1;
      return false;
    }

    if (listen(handle, 1) == -1)
    {
      LogosE("listen() failed");
      CloseSocketHandle(handle);
      handle = -1;
      return false;
    }

    sockaddr_in bound{};
    socklen_t size = sizeof(bound);
    if (getsockname(handle, (sockaddr*)&bound, &size) == -1)
    {
      LogosE("getsockname() failed");
      CloseSocketHandle(handle);
      handle = -1;
      return false;
    }

    port = ntohs(bound.sin_port);
    return true;
  }

  static bool ConnectClient(int port, SocketPair& pair)
  {
    int handle = int(socket(AF_INET, SOCK_STREAM, 0));
    if (handle == -1)
    {
      LogosE("socket() failed");
      return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (connect(handle, (sockaddr*)&addr, sizeof(addr)) == -1)
    {
      LogosE("connect() failed");
      CloseSocketHandle(handle);
      return false;
    }

    pair.Client = pair.CreateBIOSocket();
    if (pair.Client->Attach(handle) == false)
    {
      LogmeE("Attach() failed");
      CloseSocketHandle(handle);
      return false;
    }

    if (pair.Client->Configure() == false)
    {
      LogmeE("Configure() failed");
      return false;
    }

    return true;
  }

  static bool ConnectLoopbackSocket(SocketPair& pair, SocketPtr& socket, int& externalHandle)
  {
    externalHandle = -1;

    int listenHandle = -1;
    int port = 0;
    if (CreateListenSocket(listenHandle, port) == false)
      return false;

    int clientHandle = int(::socket(AF_INET, SOCK_STREAM, 0));
    if (clientHandle == -1)
    {
      CloseSocketHandle(listenHandle);
      return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (::connect(clientHandle, (sockaddr*)&addr, sizeof(addr)) == -1)
    {
      CloseSocketHandle(clientHandle);
      CloseSocketHandle(listenHandle);
      return false;
    }

    sockaddr_in peer{};
    socklen_t size = sizeof(peer);
    int accepted = int(::accept(listenHandle, (sockaddr*)&peer, &size));
    CloseSocketHandle(listenHandle);

    if (accepted == -1)
    {
      CloseSocketHandle(clientHandle);
      return false;
    }

    socket = pair.CreateBIOSocket();
    if (socket->Attach(accepted) == false)
    {
      CloseSocketHandle(clientHandle);
      CloseSocketHandle(accepted);
      return false;
    }

    if (socket->Configure() == false)
    {
      CloseSocketHandle(clientHandle);
      return false;
    }

    if (SetReceiveTimeout(clientHandle, 5000) == false)
    {
      CloseSocketHandle(clientHandle);
      return false;
    }

    externalHandle = clientHandle;
    return true;
  }

  static bool SendAll(int handle, const char* data, size_t size)
  {
    size_t sent = 0;
    while (sent < size)
    {
      int n = ::send(handle, data + sent, int(size - sent), 0);
      if (n <= 0)
        return false;

      sent += size_t(n);
    }

    return true;
  }

  static bool ReceiveExact(int handle, const char* data, size_t size)
  {
    std::vector<char> buffer(size);
    size_t received = 0;

    while (received < size)
    {
      int n = ::recv(handle, buffer.data() + received, int(size - received), 0);
      if (n <= 0)
        return false;

      received += size_t(n);
    }

    return memcmp(buffer.data(), data, size) == 0;
  }

  static bool DrainSocketTx(SocketEventLoop& loop, Socket* socket)
  {
    for (int i = 0; i < 16 && socket->TxQueue.IsEmpty() == false; ++i)
    {
      if (loop.Update(socket, socket->GetEventMaskForIO() | EVENT_CLOSE) == false)
        return false;

      SocketEventLoopResult result;
      if (loop.Wait(result, 1000) == false)
        return false;

      if (result.Skt != socket)
        continue;

      IOStat stat{};
      if (socket->ProcessIOEvents(result.Events, stat) == false)
        return false;
    }

    return socket->TxQueue.IsEmpty();
  }

  static bool PumpOneDirection(
    SocketEventLoop& loop
    , Socket* from
    , Socket* to
    , int receiverHandle
    , const char* expected
  )
  {
    const size_t expectedSize = strlen(expected);

    for (int i = 0; i < 16; ++i)
    {
      SocketEventLoopResult result;
      if (loop.Wait(result, 1000) == false)
        return false;

      if (result.Skt == nullptr)
        continue;

      IOStat stat{};
      if (result.Skt->ProcessIOEvents(result.Events, stat) == false)
        return false;

      if (loop.Update(
        result.Skt
        , result.Skt->GetEventMaskForIO() | EVENT_CLOSE
      ) == false)
        return false;

      if (result.Skt != from)
        continue;

      char buffer[1024]{};
      int n = from->Read(buffer, sizeof(buffer), 0);
      if (n <= 0)
        continue;

      bool queued = false;
      int written = to->Write(buffer, size_t(n), 0, &queued);
      if (written != n)
        return false;

      if (queued || to->TxQueue.IsEmpty() == false)
      {
        if (DrainSocketTx(loop, to) == false)
          return false;
      }

      return ReceiveExact(receiverHandle, expected, expectedSize);
    }

    return false;
  }

  static bool WaitCloseEvent(SocketEventLoop& loop, Socket* socket)
  {
    if (loop.Update(
      socket
      , socket->GetEventMaskForIO() | EVENT_CLOSE
    ) == false)
      return false;

    for (int i = 0; i < 16; ++i)
    {
      SocketEventLoopResult result;
      if (loop.Wait(result, 1000) == false)
        return false;

      if (result.Skt != socket)
        continue;

      IOStat stat{};
      socket->ProcessIOEvents(result.Events, stat);

      if (socket->PeerDisconnected())
        return true;

      if (loop.Update(
        socket
        , socket->GetEventMaskForIO() | EVENT_CLOSE
      ) == false)
        return false;
    }

    return false;
  }


  static bool WaitNoSocketEvent(SocketEventLoop& loop, int timeout)
  {
    SocketEventLoopResult result;
    if (loop.Wait(result, timeout) == false)
      return false;

    return result.Skt == nullptr
      && result.Events == 0
      && result.Operation == SocketEventLoopOperation::None;
  }

  static bool WaitSocketRead(
    SocketEventLoop& loop
    , Socket* socket
    , int timeout
    , SocketEventLoopResult& result
  )
  {
    result = SocketEventLoopResult();

    for (int i = 0; i < 16; ++i)
    {
      SocketEventLoopResult current;
      if (loop.Wait(current, timeout) == false)
        return false;

      if (current.Skt == nullptr)
        continue;

      if (current.Skt != socket)
        continue;

      if ((current.Events & EVENT_READ) == 0)
        return false;

      result = current;
      return true;
    }

    return false;
  }

  static bool ReadExactFromSocket(
    Socket* socket
    , const char* expected
  )
  {
    char buffer[1024]{};
    int n = socket->Read(buffer, sizeof(buffer), 0);
    if (n != int(strlen(expected)))
      return false;

    return memcmp(buffer, expected, strlen(expected)) == 0;
  }

  static void AcceptAndSend(int listenHandle, HEvent doneEvent)
  {
    sockaddr_in peer{};
    socklen_t size = sizeof(peer);
    int accepted = int(accept(listenHandle, (sockaddr*)&peer, &size));
    if (accepted != -1)
    {
      send(accepted, DATA, int(strlen(DATA)), 0);
      shutdown(accepted, SD_SEND);
      CloseSocketHandle(accepted);
    }

    CloseSocketHandle(listenHandle);
    SetEvent(doneEvent);
  }
}

TEST(SocketEventLoop, WakeAndStopAreDifferentEvents)
{
  auto loop = SocketEventLoop::Create();
  ASSERT_NE(loop, nullptr);

  loop->Wake();

  SocketEventLoopResult result;
  bool ok = loop->Wait(result, 1000);
  EXPECT_TRUE(ok);
  EXPECT_EQ(result.Operation, SocketEventLoopOperation::Wake);

  loop->Stop();

  result = SocketEventLoopResult();
  ok = loop->Wait(result, 1000);
  EXPECT_FALSE(ok);
  EXPECT_EQ(result.Operation, SocketEventLoopOperation::Stop);
}

TEST(SocketEventLoop, ReadEventCanDriveSocketIO)
{
  Logme::ID ch = CH;
  HEvent exitEvent = CreateNotificationEvent();
  HEvent doneEvent = CreateNotificationEvent();
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  int listenHandle = -1;
  int port = 0;
  ASSERT_TRUE(CreateListenSocket(listenHandle, port));

  std::thread server(AcceptAndSend, listenHandle, doneEvent);

  ASSERT_TRUE(ConnectClient(port, pair));

  auto loop = SocketEventLoop::Create();
  ASSERT_NE(loop, nullptr);

  int context = 7;
  ASSERT_TRUE(loop->Add(pair.Client.get(), &context, EVENT_READ | EVENT_CLOSE));

  SocketEventLoopResult result;
  bool found = false;

  for (int i = 0; i < 10 && found == false; ++i)
  {
    result = SocketEventLoopResult();
    bool ok = loop->Wait(result, 1000);
    ASSERT_TRUE(ok);

    if (result.Skt == pair.Client.get())
      found = true;
  }

  ASSERT_TRUE(found);
  EXPECT_EQ(result.Context, &context);
  EXPECT_NE(result.Events & (EVENT_READ | EVENT_CLOSE), 0);

  IOStat stat{};
  pair.Client->ProcessIOEvents(result.Events, stat);

  char buffer[128]{};
  int n = pair.Client->Read(buffer, sizeof(buffer), 0);
  EXPECT_EQ(n, int(strlen(DATA)));
  EXPECT_EQ(strcmp(buffer, DATA), 0);

  EXPECT_TRUE(loop->Remove(pair.Client.get()));

  SetEvent(exitEvent);
  WaitForSingleObject(doneEvent, 5000);
  if (server.joinable())
    server.join();

  pair.Close();
}


TEST(SocketEventLoop, ReadEventIsNotGeneratedWithoutData)
{
  Logme::ID ch = CH;
  HEvent exitEvent = CreateNotificationEvent();
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  int peer = -1;
  ASSERT_TRUE(ConnectLoopbackSocket(pair, pair.Client, peer));

  auto loop = SocketEventLoop::Create();
  ASSERT_NE(loop, nullptr);

  int context = 1;
  ASSERT_TRUE(loop->Add(pair.Client.get(), &context, EVENT_READ | EVENT_CLOSE));

  EXPECT_TRUE(WaitNoSocketEvent(*loop, 100));

  constexpr const char FIRST_PACKET[] = "first readiness payload";
  ASSERT_TRUE(SendAll(peer, FIRST_PACKET, strlen(FIRST_PACKET)));

  SocketEventLoopResult result;
  ASSERT_TRUE(WaitSocketRead(*loop, pair.Client.get(), 1000, result));
  EXPECT_EQ(result.Context, &context);

  IOStat stat{};
  ASSERT_TRUE(pair.Client->ProcessIOEvents(result.Events, stat));
  EXPECT_TRUE(ReadExactFromSocket(pair.Client.get(), FIRST_PACKET));

  ASSERT_TRUE(loop->Update(pair.Client.get(), EVENT_READ | EVENT_CLOSE));
  EXPECT_TRUE(WaitNoSocketEvent(*loop, 100));

  constexpr const char SECOND_PACKET[] = "second readiness payload";
  ASSERT_TRUE(SendAll(peer, SECOND_PACKET, strlen(SECOND_PACKET)));

  ASSERT_TRUE(WaitSocketRead(*loop, pair.Client.get(), 1000, result));

  stat = IOStat{};
  ASSERT_TRUE(pair.Client->ProcessIOEvents(result.Events, stat));
  EXPECT_TRUE(ReadExactFromSocket(pair.Client.get(), SECOND_PACKET));

  EXPECT_TRUE(loop->Remove(pair.Client.get()));

  SetEvent(exitEvent);
  pair.Close();
  CloseSocketHandle(peer);
}

TEST(SocketEventLoop, BidirectionalForwardingMatchesRawManagerUsage)
{
  Logme::ID ch = CH;
  HEvent exitEvent = CreateNotificationEvent();
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  int clientPeer = -1;
  int serverPeer = -1;

  ASSERT_TRUE(ConnectLoopbackSocket(pair, pair.Client, clientPeer));
  ASSERT_TRUE(ConnectLoopbackSocket(pair, pair.Server, serverPeer));

  auto loop = SocketEventLoop::Create();
  ASSERT_NE(loop, nullptr);

  int clientContext = 1;
  int serverContext = 2;

  ASSERT_TRUE(loop->Add(pair.Client.get(), &clientContext, EVENT_READ | EVENT_CLOSE));
  ASSERT_TRUE(loop->Add(pair.Server.get(), &serverContext, EVENT_READ | EVENT_CLOSE));

  ASSERT_TRUE(SendAll(clientPeer, CLIENT_TO_SERVER, strlen(CLIENT_TO_SERVER)));
  EXPECT_TRUE(PumpOneDirection(
    *loop
    , pair.Client.get()
    , pair.Server.get()
    , serverPeer
    , CLIENT_TO_SERVER
  ));

  ASSERT_TRUE(SendAll(serverPeer, SERVER_TO_CLIENT, strlen(SERVER_TO_CLIENT)));
  EXPECT_TRUE(PumpOneDirection(
    *loop
    , pair.Server.get()
    , pair.Client.get()
    , clientPeer
    , SERVER_TO_CLIENT
  ));

  shutdown(clientPeer, SD_SEND);
  EXPECT_TRUE(WaitCloseEvent(*loop, pair.Client.get()));

  EXPECT_TRUE(loop->Remove(pair.Client.get()));
  EXPECT_TRUE(loop->Remove(pair.Server.get()));

  SetEvent(exitEvent);
  pair.Close();

  CloseSocketHandle(clientPeer);
  CloseSocketHandle(serverPeer);
}
