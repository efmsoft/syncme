#include <atomic>
#include <cstring>
#include <memory>
#include <thread>

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

  static void CloseSocketHandle(int handle)
  {
    if (handle != -1)
      closesocket(handle);
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
