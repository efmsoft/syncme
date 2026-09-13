#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/Async/AsyncStream.h>
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

  // Drives stream->StartRead() through the engine until expectedTotal bytes
  // have been collected in out, reusing (regrowing, never reallocating past
  // the first call) the same buffer object across calls -- the pattern a
  // caller uses to avoid allocating a fresh read buffer on every cycle
  // (e.g. H2::ClientReadBuffer/ServerReadBuffer in Dll). Result::Bytes is
  // read on every call and trusted as the sole valid-length signal; nothing
  // here ever looks at reusableBuffer->size() to decide what is valid.
  //
  // Also asserts result.Buffer->size() == result.Bytes on every read: this
  // is the separate, load-bearing invariant that Raw::QueueAsyncWrite (and
  // WebSocket's own write queue) depend on, forwarding the read buffer on
  // as a write payload using its size() with no separate length alongside
  // it. A change that stops the engine shrinking the buffer to the actual
  // byte count -- exactly the regression introduced and then reverted
  // earlier while working on this buffer-reuse optimization -- would break
  // that silently for Raw without this check, since Result::Bytes alone
  // stays correct either way.
  static bool ReadAllViaEngine(
    Sockets::Async::AsyncEngine& engine
    , const Sockets::Async::AsyncStreamPtr& stream
    , Sockets::IO::BufferPtr& reusableBuffer
    , size_t capacity
    , size_t expectedTotal
    , std::string& out
  )
  {
    out.clear();

    while (out.size() < expectedTotal)
    {
      if (reusableBuffer == nullptr)
        reusableBuffer = std::make_shared<Sockets::IO::Buffer>(capacity);
      else
        reusableBuffer->resize(capacity);

      if (stream->StartRead(reusableBuffer) == false)
        return false;

      bool got = false;
      for (int i = 0; i < 16 && got == false; ++i)
      {
        Sockets::Async::Result result;
        if (engine.Wait(result, 1000) == false)
          return false;

        if (result.Stream != stream || result.Op != Sockets::Async::Operation::Read)
          continue;

        if (result.Buffer == nullptr || result.Bytes == 0)
          return false;

        EXPECT_EQ(result.Buffer->size(), result.Bytes)
          << "engine no longer shrinks the read buffer to the actual byte "
             "count; Raw::QueueAsyncWrite forwards this buffer on as a "
             "write payload using size() alone";

        if (result.Buffer->size() != result.Bytes)
          return false;

        out.append(result.Buffer->data(), result.Bytes);
        got = true;
      }

      if (got == false)
        return false;
    }

    return out.size() == expectedTotal;
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

TEST(AsyncEngine, ReadDeliversExactBytes)
{
  Logme::ID ch = CH;
  HEvent exitEvent = CreateNotificationEvent();
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  int peer = -1;
  ASSERT_TRUE(ConnectLoopbackSocket(pair, pair.Client, peer));

  auto engine = Sockets::Async::AsyncEngine::Create();
  ASSERT_NE(engine, nullptr);

  int context = 1;
  Sockets::Async::AsyncStreamPtr stream;
  ASSERT_TRUE(engine->Add(pair.Client.get(), &context, stream));
  ASSERT_NE(stream, nullptr);

  const std::string payload = "async engine single read payload";
  ASSERT_TRUE(SendAll(peer, payload.data(), payload.size()));

  Sockets::IO::BufferPtr buffer;
  std::string received;
  EXPECT_TRUE(ReadAllViaEngine(*engine, stream, buffer, 4096, payload.size(), received));
  EXPECT_EQ(received, payload);

  EXPECT_TRUE(engine->Remove(stream.get()));

  SetEvent(exitEvent);
  pair.Close();
  CloseSocketHandle(peer);
}

// Regression test for the H2 read-buffer reuse change (H2::StartAsyncRead
// keeping and regrowing one buffer per direction instead of allocating
// fresh every read) and for IO::Buffer's DefaultInitAllocator (growing the
// buffer back up no longer zero-fills the new range). Neither change is
// exercised by any other test in this suite. A large read followed by a
// tiny one followed by another large one, each with a distinct byte value
// and through the SAME buffer object, would surface either bug as a wrong
// length or as bytes from an earlier round leaking into a later one.
TEST(AsyncEngine, ReusedReadBufferAcrossVaryingSizesDoesNotCorruptData)
{
  Logme::ID ch = CH;
  HEvent exitEvent = CreateNotificationEvent();
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  int peer = -1;
  ASSERT_TRUE(ConnectLoopbackSocket(pair, pair.Client, peer));

  auto engine = Sockets::Async::AsyncEngine::Create();
  ASSERT_NE(engine, nullptr);

  int context = 1;
  Sockets::Async::AsyncStreamPtr stream;
  ASSERT_TRUE(engine->Add(pair.Client.get(), &context, stream));
  ASSERT_NE(stream, nullptr);

  constexpr size_t CAPACITY = 4096;
  Sockets::IO::BufferPtr buffer;
  std::string received;

  // Round 1: a read close to the buffer's full capacity.
  const std::string big(CAPACITY - 96, 'A');
  ASSERT_TRUE(SendAll(peer, big.data(), big.size()));
  ASSERT_TRUE(ReadAllViaEngine(*engine, stream, buffer, CAPACITY, big.size(), received));
  EXPECT_EQ(received, big);

  // Round 2: reuse the same (shrunk-to-big.size()) buffer object for a tiny
  // read. This is where stale capacity or stale 'A' padding leaking back in
  // as if it were valid data would show up.
  const std::string tiny = "tiny-payload";
  ASSERT_TRUE(SendAll(peer, tiny.data(), tiny.size()));
  ASSERT_TRUE(ReadAllViaEngine(*engine, stream, buffer, CAPACITY, tiny.size(), received));
  EXPECT_EQ(received, tiny);

  // Round 3: grow the same buffer back up for another large read, with a
  // third distinct byte value, confirming growing again after a small read
  // is equally clean.
  const std::string big2(CAPACITY - 200, 'C');
  ASSERT_TRUE(SendAll(peer, big2.data(), big2.size()));
  ASSERT_TRUE(ReadAllViaEngine(*engine, stream, buffer, CAPACITY, big2.size(), received));
  EXPECT_EQ(received, big2);

  EXPECT_TRUE(engine->Remove(stream.get()));

  SetEvent(exitEvent);
  pair.Close();
  CloseSocketHandle(peer);
}

#ifndef _WIN32
TEST(AsyncEngine, HalfCloseWithoutPendingReadDoesNotSpin)
{
  Logme::ID ch = CH;
  HEvent exitEvent = CreateNotificationEvent();
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  int peer = -1;
  ASSERT_TRUE(ConnectLoopbackSocket(pair, pair.Client, peer));

  auto engine = Sockets::Async::AsyncEngine::Create();
  ASSERT_NE(engine, nullptr);

  int context = 1;
  Sockets::Async::AsyncStreamPtr stream;
  ASSERT_TRUE(engine->Add(pair.Client.get(), &context, stream));
  ASSERT_NE(stream, nullptr);

  ASSERT_EQ(shutdown(peer, SD_SEND), 0);

  auto waitResult = std::async(
    std::launch::async
    , [&engine]() {
      Sockets::Async::Result result;
      bool ok = engine->Wait(result, 100);
      return std::make_pair(ok, result.Op);
    }
  );

  const auto status = waitResult.wait_for(std::chrono::milliseconds(1000));
  const bool spinDetected = status != std::future_status::ready;

  if (spinDetected)
    engine->Stop();

  const auto result = waitResult.get();

  EXPECT_FALSE(spinDetected);
  if (!spinDetected)
  {
    EXPECT_TRUE(result.first);
    EXPECT_EQ(result.second, Sockets::Async::Operation::None);
  }

  EXPECT_TRUE(engine->Remove(stream.get()));

  SetEvent(exitEvent);
  pair.Close();
  CloseSocketHandle(peer);
}

TEST(AsyncEngine, PeerShutdownCompletesPendingWriteWithoutSpin)
{
  Logme::ID ch = CH;
  HEvent exitEvent = CreateNotificationEvent();
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  int peer = -1;
  ASSERT_TRUE(ConnectLoopbackSocket(pair, pair.Client, peer));

  int sendBufferSize = 4096;
  ASSERT_EQ(
    setsockopt(
      pair.Client->Handle
      , SOL_SOCKET
      , SO_SNDBUF
      , (const char*)&sendBufferSize
      , sizeof(sendBufferSize)
    )
    , 0
  );

  auto engine = Sockets::Async::AsyncEngine::Create();
  ASSERT_NE(engine, nullptr);

  int context = 1;
  Sockets::Async::AsyncStreamPtr stream;
  ASSERT_TRUE(engine->Add(pair.Client.get(), &context, stream));
  ASSERT_NE(stream, nullptr);

  auto buffer = std::make_shared<Sockets::IO::Buffer>(1024 * 1024, 'x');
  Sockets::Async::BufferChain buffers;
  ASSERT_TRUE(buffers.Add(buffer));
  ASSERT_TRUE(stream->StartWrite(buffers));

  ASSERT_EQ(shutdown(peer, SHUT_RDWR), 0);

  auto waitResult = std::async(
    std::launch::async
    , [&engine]() {
      Sockets::Async::Result result;
      bool ok = engine->Wait(result, 1000);
      return std::make_pair(ok, result);
    }
  );

  const auto status = waitResult.wait_for(std::chrono::milliseconds(2000));
  const bool spinDetected = status != std::future_status::ready;

  if (spinDetected)
    engine->Stop();

  const auto result = waitResult.get();

  EXPECT_FALSE(spinDetected);
  if (!spinDetected)
  {
    EXPECT_TRUE(result.first);
    EXPECT_EQ(result.second.Op, Sockets::Async::Operation::Error);
    EXPECT_NE(result.second.Error, 0);
  }

  EXPECT_TRUE(engine->Remove(stream.get()));

  SetEvent(exitEvent);
  pair.Close();
  CloseSocketHandle(peer);
}

#endif
