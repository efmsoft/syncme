#include <thread>

#include <gtest/gtest.h>

#include <Syncme/Logger/Log.h>
#include <Syncme/ProcessThreadId.h>
#include <Syncme/SetThreadName.h>
#include <Syncme/Sleep.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/ThreadPool/Pool.h>

using namespace Syncme;

static const int ServerPort = 2345;
static const size_t NumClients = 129; // OPTIONS::GROW_SIZE * 2 + 1
static const char* Data1 = "Hello";

static void RenameThread(const char* what, size_t index, bool sender)
{
  uint32_t id = uint32_t(Syncme::GetCurrentThreadId() & 0xFFFFFFFF);

  char buffer[32]{};
  sprintf(
    buffer
    , "%s%s#%zu#%x"
    , sender ? "*" : ""
    , what
    , index
    , id
  );

  SET_CUR_THREAD_NAME(buffer);
}

static void server_thread(int client, size_t index, bool send)
{
  RenameThread("st", index, send);

  Logme::ID ch = CH;
  HEvent exitEvent = CreateNotificationEvent();
  ConfigPtr config = std::make_shared<Config>();

  SocketPair pair(ch, exitEvent, config);
  pair.Client = pair.CreateBIOSocket();

  auto f = pair.Client->Attach(client);
  if (f == false)
  {
    LogmeE("Client->Attach() failed");
    exit(1);
  }

  f = pair.Client->Configure();
  if (f == false)
  {
    LogmeE("Client->Configure() failed");
    exit(1);
  }

  std::vector<char> buffer(32);
  int n = pair.Client->Read(buffer);
  if (n == -1)
  {
    LogmeE("Failed to receive client request");
    exit(1);
  }

  Syncme::Sleep(rand() % 5000);

  if (send)
  {
    n = pair.Client->WriteStr(Data1);
    if (n == -1)
    {
      LogmeE("Failed to send data");
      exit(1);
    }
  }

  pair.Close();
}

static void listener_thread(
  ThreadPool::Pool& pool
  , HEvent readyEvent
  , HEvent serverComplete
)
{
  SET_CUR_THREAD_NAME("listener");

  int h = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (h == -1)
  {
    LogosE("socket(SOCK_STREAM) failed");
    exit(1);
  }

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

  rc = listen(h, 1);
  if (rc == -1)
  {
    LogosE("listen() failed");
    exit(1);
  }

  SetEvent(readyEvent);

  EventArray threads;
  size_t sender = rand() % NumClients;

  while (threads.size() < NumClients)
  {
    sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    int client = (int)accept(h, (sockaddr*)&peer, &len);
    if (client == -1)
    {
      LogosE("accept() failed");
      exit(1);
    }

    size_t index = threads.size();
    bool send = sender == index;
    threads.push_back(pool.Run(std::bind(server_thread, client, index, send)));
  }

  WaitForMultipleObjects(threads, true);
  closesocket(h);

  SetEvent(serverComplete);
  LogmeI("server_thread signalled clientComplete");
}

static void client_thread(HEvent& exitEvent, size_t index)
{
  RenameThread("ct", index, false);

  Logme::ID ch = CH;
  ConfigPtr config = std::make_shared<Config>();

  int h = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (h == -1)
  {
    LogosE("socket(SOCK_STREAM) failed");
    exit(1);
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(ServerPort);
  int rc = connect(h, (sockaddr*)&addr, sizeof(addr));
  if (rc == -1)
  {
    LogosE("connect() failed");
    exit(1);
  }

  SocketPair pair(ch, exitEvent, config);
  pair.Server = pair.CreateBIOSocket();

  auto f = pair.Server->Attach(h);
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

  int n = pair.Server->WriteStr(std::to_string(index));
  if (n == -1)
  {
    LogmeE("Server->WriteStr() failed");
    exit(1);
  }

  std::vector<char> buffer(32);
  n = pair.Server->Read(buffer);
  if (n > 0)
  {
    if (std::string(Data1) != &buffer[0])
    {
      LogmeE("Unexpected data from server");
      exit(1);
    }
  }

  pair.Close();
}

TEST(EventQueue, Grow)
{
#ifdef _WIN32
  WSADATA wsaData{};
  WORD wVersionRequested = MAKEWORD(2, 2);
  int e = WSAStartup(wVersionRequested, &wsaData);
  EXPECT_EQ(e, 0);
#else
  void BlockSignal(int signal_to_block);
  BlockSignal(SIGPIPE);
#endif

  HEvent readyEvent = CreateNotificationEvent();
  HEvent completeEvent = CreateNotificationEvent();

  ThreadPool::Pool pool;
  std::shared_ptr<std::jthread> listener;

  listener = std::make_shared<std::jthread>(
    listener_thread
    , std::ref(pool)
    , readyEvent
    , completeEvent
    );

  WaitForSingleObject(readyEvent);

  EventArray threads;
  while (threads.size() < NumClients)
    threads.push_back(pool.Run(std::bind(&client_thread, completeEvent, threads.size())));

  listener.reset();
  WaitForMultipleObjects(threads, true);

  pool.Stop();

#ifdef _WIN32
  WSACleanup();
#endif
}