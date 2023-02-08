#include <thread>

#include <gtest/gtest.h>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/SetThreadName.h>
#include <Syncme/Sockets/SocketPair.h>

using namespace Syncme;

bool EnableLogging = true;

static const char* Data1 = "Hello World!";
static const char* Data2 = "http/1.1 200 OK";
static const int ServerPort = 1234;

#pragma warning(disable : 28193)

#ifndef _WIN32
void BlockSignal(int signal_to_block)
{
  sigset_t old_state;
  sigprocmask(SIG_BLOCK, nullptr, &old_state);

  sigset_t set = old_state;
  sigaddset(&set, signal_to_block);

  pthread_sigmask(SIG_BLOCK, &set, nullptr);
}
#endif

static void server_thread(SocketPair& pair, HEvent& readyEvent, HEvent& serverComplete)
{
  SET_CUR_THREAD_NAME(__FUNCTION__);

  int h = (int)socket(AF_INET, SOCK_STREAM, 0);
  EXPECT_NE(h, -1);

  int reuse = 1;
  if (setsockopt(h, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
  {
    LogosE("setsockopt(SO_REUSEADDR) failed");
    exit(1);
  }

  linger lin{};
  lin.l_onoff = true;
  lin.l_linger = 0;
  if (setsockopt(h, SOL_SOCKET, SO_LINGER, (const char*)&lin, sizeof(lin)) < 0)
  {
    LogosE("setsockopt(SO_LINGER) failed");
    exit(1);
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(ServerPort);
  int rc = bind(h, (struct sockaddr*)&addr, sizeof(addr));
  if (rc == -1)
  {
    LogosE("bind() failed");
    exit(1);
  }
  EXPECT_GE(rc, -1);

  rc = listen(h, 1);
  if (rc == -1)
  {
    LogosE("listen() failed");
    exit(1);
  }

  SetEvent(readyEvent);

  sockaddr_in peer{};
  socklen_t len = sizeof(peer);
  int client = (int)accept(h, (sockaddr*)&peer, &len);
  if (client == -1)
  {
    LogosE("accept() failed");
    exit(1);
  }
  EXPECT_NE(client, -1);

  auto f = pair.Client->Attach(client);
  EXPECT_EQ(f, true);

  f = pair.Client->Configure();
  EXPECT_EQ(f, true);

  int n = pair.Client->Write(Data1, strlen(Data1));
  if (n == -1)
  {
    LogosE("Write() failed");
    exit(1);
  }
  EXPECT_EQ(n, strlen(Data1));

  std::vector<char> buffer(64 * 1024);
  n = pair.Client->Read(buffer);
  if (n == -1)
  {
    LogosE("Read() failed");
    exit(1);
  }
  EXPECT_EQ(n, (int)strlen(Data2));
  EXPECT_EQ(strcmp(&buffer[0], Data2), 0);

  SetEvent(serverComplete);
  closesocket(h);

  LogmeI("server_thread signalled clientComplete");
}

void client_thread(SocketPair& pair, HEvent& clientComplete)
{
  SET_CUR_THREAD_NAME(__FUNCTION__);

  int h = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (h == -1)
  {
    LogosE("socket() failed");
    exit(1);
  }
  EXPECT_NE(h, -1);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(ServerPort);
  int rc = connect(h, (sockaddr*)&addr, sizeof(addr));
  if (rc == -1)
  {
    LogosE("connect failed");
    exit(1);
  }
  EXPECT_NE(rc, -1);

  auto f = pair.Server->Attach(h);
  EXPECT_EQ(f, true);

  f = pair.Server->Configure();
  EXPECT_EQ(f, true);

  std::vector<char> buffer(64 * 1024);
  auto n = pair.Server->Read(buffer);
  if (n == -1)
  {
    LogosE("Read() failed");
    exit(1);
  }
  EXPECT_EQ(n, (int)strlen(Data1));
  EXPECT_EQ(strcmp(&buffer[0], Data1), 0);

  n = pair.Server->Write(Data2, strlen(Data2));
  if (n == -1)
  {
    LogosE("Write() failed");
    exit(1);
  }
  EXPECT_EQ(n, strlen(Data2));

  SetEvent(clientComplete);
  LogmeI("client_thread signalled clientComplete");
}

TEST(Sockets, basic)
{
#ifdef _WIN32
  WSADATA wsaData{};
  WORD wVersionRequested = MAKEWORD(2, 2);
  int e = WSAStartup(wVersionRequested, &wsaData);
  EXPECT_EQ(e, 0);
#else
  BlockSignal(SIGPIPE);
#endif

  Logme::ID ch = CH;
  HEvent exitEvent = CreateNotificationEvent();
  HEvent readyEvent = CreateNotificationEvent();
  HEvent serverComplete = CreateNotificationEvent();
  HEvent clientComplete = CreateNotificationEvent();
  ConfigPtr config = std::make_shared<Config>();

  SocketPair pair(ch, exitEvent, config);
  pair.Client = pair.CreateBIOSocket();
  pair.Server = pair.CreateBIOSocket();

  std::jthread sender(server_thread, std::ref(pair), std::ref(readyEvent), std::ref(serverComplete));
  auto rc = WaitForSingleObject(readyEvent);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);

  std::jthread receiver(client_thread, std::ref(pair), std::ref(clientComplete));

  EventArray object(serverComplete, clientComplete);
  rc = WaitForMultipleObjects(object, true, FOREVER);
  EXPECT_EQ(rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1, true);

  pair.Close();

#ifdef _WIN32
  WSACleanup();
#endif
}