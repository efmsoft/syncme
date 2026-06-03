#include <atomic>
#include <cassert>
#include <functional>
#include <gtest/gtest.h>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/ThreadPool/Pool.h>
#include <Syncme/SetThreadName.h>
#include <Syncme/Sockets/SocketPair.h>

using namespace Syncme;

static const size_t ChunkSize = 4 * 1024;
static const size_t DataSize = 512 * 1024;

static void ServerThread(int client, HEvent dataReadyEvent, HEvent serverDoneEvent)
{
  SET_CUR_THREAD_NAME("server");
  printf("Server connection established\n");

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

  // Wait for data from client thread. The client will send 512k of data.
  // Then we will try to read them by 4k chunks.
  WaitForSingleObject(dataReadyEvent, FOREVER);
  printf("Data ready is signalled\n");

  for (size_t cb = 0; cb < DataSize;)
  {
    std::vector<char> buffer(DataSize * ChunkSize);
    int n = pair.Client->Read(buffer, 10000);
    if (n <= 0)
    {
      LogmeE("Failed to receive client request");
      exit(1);
    }

    cb += n;
    LogmeI("Received %i bytes", n);
  }
  
  printf("All data blocks received\n");

  SetEvent(serverDoneEvent);
  pair.Close();
  printf("Exiting server connection\n");
}

static void ListenerThread(
  ThreadPool::Pool& pool
  , HEvent readyEvent
  , HEvent dataReadyEvent
  , HEvent serverDoneEvent
  , std::atomic<int>& serverPort
)
{
  SET_CUR_THREAD_NAME("listener");
  printf("Listener started\n");

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

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(0);
  int rc = bind(h, (struct sockaddr*)&addr, sizeof(addr));
  if (rc == -1)
  {
    LogosE("bind() failed");
    exit(1);
  }

  sockaddr_in bound{};
  socklen_t boundLen = sizeof(bound);
  rc = getsockname(h, (sockaddr*)&bound, &boundLen);
  if (rc == -1)
  {
    LogosE("getsockname() failed");
    exit(1);
  }

  serverPort.store(ntohs(bound.sin_port));

  rc = listen(h, 1);
  if (rc == -1)
  {
    LogosE("listen() failed");
    exit(1);
  }

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
  threads.push_back(
    pool.Run(
      std::bind(
        ServerThread
        , client
        , std::ref(dataReadyEvent)
        , std::ref(serverDoneEvent)
      )
    )
  );

  WaitForMultipleObjects(threads, true);
  closesocket(h);
  
  printf("Exiting listener\n");
}

static void ClientThread(
  HEvent readyEvent
  , HEvent dataReadyEvent
  , HEvent serverDoneEvent
  , std::atomic<int>& serverPort
)
{
  SET_CUR_THREAD_NAME("client");
  printf("Client thread is started\n");

  WaitForSingleObject(readyEvent, FOREVER);

  Logme::ID ch = CH;
  ConfigPtr config = std::make_shared<Config>();

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

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((uint16_t)serverPort.load());
  int rc = connect(h, (sockaddr*)&addr, sizeof(addr));
  if (rc == -1)
  {
    LogosE("connect() failed");
    exit(1);
  }

  HEvent exitEvent = CreateNotificationEvent();
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

  std::vector<char> chunk(ChunkSize);
  for (size_t i = 0; i < ChunkSize; i++)
    chunk[i] = (char)(unsigned char)i;

  bool needFlush = false;
  for (size_t cb = 0; cb < DataSize; cb += ChunkSize)
  {
    bool queued = false;
    int n = pair.Server->Write(chunk, FOREVER, &queued);
    if (n == -1)
    {
      LogmeE("Server->WriteStr() failed");
      exit(1);
    }

    assert(n == ChunkSize);

    if (queued)
      needFlush = true;

    LogmeI("Sent chunk %zu", cb / ChunkSize);
  }

  SetEvent(dataReadyEvent);

  if (needFlush && pair.Server->Flush(10000) == false)
  {
    LogmeE("Server->Flush() failed");
    exit(1);
  }

  if (WaitForSingleObject(serverDoneEvent, 10000) != WAIT_RESULT::OBJECT_0)
  {
    LogmeE("Server did not finish receiving data");
    exit(1);
  }

  pair.Close();
  printf("Exiting client\n");
}

TEST(EventQueue, LT_ET)
{
  HEvent readyEvent = CreateNotificationEvent();
  HEvent dataReadyEvent = CreateNotificationEvent();
  HEvent serverDoneEvent = CreateNotificationEvent();
  std::atomic<int> serverPort{ 0 };

  EventArray threads;
  ThreadPool::Pool pool;
  threads.push_back(
    pool.Run(
      std::bind(
        &ListenerThread
        , std::ref(pool)
        , std::ref(readyEvent)
        , std::ref(dataReadyEvent)
        , std::ref(serverDoneEvent)
        , std::ref(serverPort)
      )
    )
  );
  threads.push_back(
    pool.Run(
      std::bind(
        &ClientThread
        , std::ref(readyEvent)
        , std::ref(dataReadyEvent)
        , std::ref(serverDoneEvent)
        , std::ref(serverPort)
      )
    )
  );

  WaitForMultipleObjects(threads, true);
  pool.Stop();
}
