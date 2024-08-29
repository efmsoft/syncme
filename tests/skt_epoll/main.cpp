#include <cassert>
#include <functional>
#include <gtest/gtest.h>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sleep.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/ThreadPool/Pool.h>
#include <Syncme/TickCount.h>

using namespace Syncme;

enum class TestPort
{
  Timeout = 22222,
  Exit,
  Close,
  Break,
  Receive,
};

static bool PrepareEventClient(int port, SocketPair& pair)
{
  int h = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (h == -1)
  {
    LogosE("socket() failed");
    EXPECT_NE(h, -1);
    return false;
  }

  int reuse = 1;
  int n = setsockopt(h, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
  if (n == -1)
  {
    LogosE("setsockopt(SO_REUSEADDR) failed");
    EXPECT_NE(n, -1);
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  n = connect(h, (sockaddr*)&addr, sizeof(addr));
  if (n == -1)
  {
    LogosE("connect() failed");
    EXPECT_NE(n, -1);
    return false;
  }
  
  printf("client socket connected\n");
  pair.Client = pair.CreateBIOSocket();

  auto f = pair.Client->Attach(h);
  if (f == false)
  {
    LogmeE("Attach() failed");
    EXPECT_EQ(f, true);
    return false;
  }

  f = pair.Client->Configure();
  if (f == false)
  {
    LogmeE("Client->Configure() failed");
    EXPECT_EQ(f, true);
    return false;
  }

  return true;
}

static void ServerThread(int client, SocketPair& pair)
{
  printf("Server accepted connection\n");
  pair.Server = pair.CreateBIOSocket();

  auto f = pair.Server->Attach(client);
  if (f == false)
  {
    LogmeE("Server->Attach() failed");
    exit(1);
  }

  f = pair.Server->Configure();
  if (f == false)
  {
    LogmeE("Server->Configure() failed");
    exit(1);
  }

  while (!pair.Closing())
  {
    char buffer[1024]{};
    int n = pair.Server->Read(buffer, sizeof(buffer), 1000);
    if (n < 0)
      break;

    if (n > 0)
    {
      if (rand() & 1)
      {
        printf("returning packet back with a delay\n");
        Sleep(2000);
      }

      pair.Server->Write(buffer, n);
    }
  }

  WaitForSingleObject(pair.GetExitEvent());
  printf("closing server thread\n");
}

static void ListenerThread(int h, SocketPair& pair, ThreadPool::Pool& pool, HEvent& readyEvent)
{
  SetEvent(readyEvent);
  printf("Ready is signalled\n");

  sockaddr_in peer{};
  socklen_t len = sizeof(peer);
  int client = (int)accept(h, (sockaddr*)&peer, &len);
  if (client == -1)
  {
    LogosE("accept() failed");
    exit(1);
  }

  EventArray threads;
  threads.push_back(pool.Run(std::bind(ServerThread, client, std::ref(pair))));

  WaitForMultipleObjects(threads, true);
  closesocket(h);
  
  printf("Exiting listener\n");
}

static HEvent StartEventServer(int port, SocketPair& pair, ThreadPool::Pool& pool)
{
  int h = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (h == -1)
  {
    LogosE("socket(SOCK_STREAM) failed");
    return HEvent();
  }

  int reuse = 1;
  if (setsockopt(h, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
  {
    LogosE("setsockopt(SO_REUSEADDR) failed");
    return HEvent();
  }

  linger lin{};
  lin.l_onoff = true;
  lin.l_linger = 0;
  if (setsockopt(h, SOL_SOCKET, SO_LINGER, (const char*)&lin, sizeof(lin)) < 0)
  {
    LogosE("setsockopt(SO_LINGER) failed");
    return HEvent();
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  int rc = bind(h, (struct sockaddr*)&addr, sizeof(addr));
  if (rc == -1)
  {
    LogosE("bind() failed");
    return HEvent();
  }

  rc = listen(h, 1);
  if (rc == -1)
  {
    LogosE("listen() failed");
    return HEvent();
  }

  HEvent readyEvent = CreateNotificationEvent();

  auto server = pool.Run(
    std::bind(&ListenerThread, h, std::ref(pair), std::ref(pool), std::ref(readyEvent))
  );

  WaitForSingleObject(readyEvent);
  return server;
}

TEST(WaitRxReady, timeout)
{
  Logme::ID ch = CH;
  const int port = int(TestPort::Timeout);

  HEvent exitEvent = CreateNotificationEvent();  
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  ThreadPool::Pool pool;
  HEvent server = StartEventServer(port, pair, pool);
  if (server == nullptr)
  {
    EXPECT_NE(server, nullptr);
    return;
  }

  bool clientInitDone = PrepareEventClient(port, pair);
  if (clientInitDone == false)
  {
    EXPECT_EQ(clientInitDone, true);
    return;
  }

#if SKTEPOLL
  auto rc = pair.Client->FastWaitForMultipleObjects(2000);
  EXPECT_EQ(rc, WAIT_RESULT::TIMEOUT);
#endif

  SetEvent(exitEvent);
  WaitForSingleObject(server);

  pool.Stop();
}

static void SetEventWithDelay(HEvent& ev, int delay)
{
  Syncme::Sleep(delay);

  printf("setting event\n");
  SetEvent(ev);
}

TEST(WaitRxReady, exit)
{
  Logme::ID ch = CH;
  const int port = int(TestPort::Exit);

  HEvent exitEvent = CreateNotificationEvent();  
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  ThreadPool::Pool pool;
  HEvent server = StartEventServer(port, pair, pool);
  if (server == nullptr)
  {
    EXPECT_NE(server, nullptr);
    return;
  }

  bool clientInitDone = PrepareEventClient(port, pair);
  if (clientInitDone == false)
  {
    EXPECT_EQ(clientInitDone, true);
    return;
  }

  EventArray threads;
  threads.push_back(server);

#if SKTEPOLL
  auto t = pool.Run(std::bind(&SetEventWithDelay, std::ref(exitEvent), 2000));
  threads.push_back(t);

  auto rc = pair.Client->FastWaitForMultipleObjects(5000);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);
#endif

  SetEvent(exitEvent);
  WaitForMultipleObjects(threads, true);

  pool.Stop();
}

static void ClosePairWithDelay(SocketPair& pair, int delay)
{
  Syncme::Sleep(delay);

  printf("closing pair\n");
  pair.Close();
}

TEST(WaitRxReady, close)
{
  Logme::ID ch = CH;
  const int port = int(TestPort::Close);

  HEvent exitEvent = CreateNotificationEvent();  
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  ThreadPool::Pool pool;
  HEvent server = StartEventServer(port, pair, pool);
  if (server == nullptr)
  {
    EXPECT_NE(server, nullptr);
    return;
  }

  bool clientInitDone = PrepareEventClient(port, pair);
  if (clientInitDone == false)
  {
    EXPECT_EQ(clientInitDone, true);
    return;
  }

  EventArray threads;
  threads.push_back(server);

#if SKTEPOLL
  auto t = pool.Run(std::bind(&ClosePairWithDelay, std::ref(pair), 2000));
  threads.push_back(t);

  auto rc = pair.Client->FastWaitForMultipleObjects(5000);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_1);
#endif

  SetEvent(exitEvent);
  WaitForMultipleObjects(threads, true);

  pool.Stop();
}

static void BreakReadWithDelay(SocketPair& pair, int delay)
{
  Syncme::Sleep(delay);

  printf("break read\n");
  pair.Client->StopPendingRead();
}

TEST(WaitRxReady, break)
{
  Logme::ID ch = CH;
  const int port = int(TestPort::Break);

  HEvent exitEvent = CreateNotificationEvent();  
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  ThreadPool::Pool pool;
  HEvent server = StartEventServer(port, pair, pool);
  if (server == nullptr)
  {
    EXPECT_NE(server, nullptr);
    return;
  }

  bool clientInitDone = PrepareEventClient(port, pair);
  if (clientInitDone == false)
  {
    EXPECT_EQ(clientInitDone, true);
    return;
  }

  EventArray threads;
  threads.push_back(server);

#if SKTEPOLL
  auto t = pool.Run(std::bind(&BreakReadWithDelay, std::ref(pair), 500));
  threads.push_back(t);

  auto rc = pair.Client->FastWaitForMultipleObjects(5000);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_3);
#endif

  SetEvent(exitEvent);
  WaitForMultipleObjects(threads, true);

  pool.Stop();
}

TEST(WaitRxReady, receive)
{
  srand(GetTimeInMillisec());

  Logme::ID ch = CH;
  const int port = int(TestPort::Receive);

  HEvent exitEvent = CreateNotificationEvent();  
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  ThreadPool::Pool pool;
  HEvent server = StartEventServer(port, pair, pool);
  if (server == nullptr)
  {
    EXPECT_NE(server, nullptr);
    return;
  }

  bool clientInitDone = PrepareEventClient(port, pair);
  if (clientInitDone == false)
  {
    EXPECT_EQ(clientInitDone, true);
    return;
  }

  EventArray threads;
  threads.push_back(server);

#if SKTEPOLL
  pair.Client->Write("hello", 5);

  auto rc = pair.Client->FastWaitForMultipleObjects(50000);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_2);
#endif

  SetEvent(exitEvent);
  WaitForMultipleObjects(threads, true);

  pool.Stop();
}
