#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/SocketPair.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace Syncme;

#ifndef _WIN32
TEST(Sockets, bounded_flush_does_not_wait_for_busy_io_lock)
{
  Logme::ID ch = CH;
  HEvent exitEvent = CreateNotificationEvent();
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  int sockets[2]{};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);

  pair.Client = pair.CreateBIOSocket();
  ASSERT_TRUE(pair.Client->Attach(sockets[0]));
  ASSERT_TRUE(pair.Client->SwitchToUnblockingMode());
  ASSERT_TRUE(pair.Client->TxQueue.Append("X", 1));

  pair.Client->IOLock.lock();
  bool flushed = true;
  const auto start = std::chrono::steady_clock::now();
  std::thread worker([&]() { flushed = pair.Client->Flush(40); });
  worker.join();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - start
  ).count();
  pair.Client->IOLock.unlock();

  EXPECT_FALSE(flushed);
  EXPECT_GE(elapsed, 25);
  EXPECT_LT(elapsed, 1000);
  EXPECT_FALSE(pair.Client->TxQueue.IsEmpty());

  pair.Close(SocketPairCloseMode::Fast);
  close(sockets[1]);
  CloseHandle(exitEvent);
}

TEST(Sockets, bounded_flush_does_not_wait_for_busy_tx_lock)
{
  Logme::ID ch = CH;
  HEvent exitEvent = CreateNotificationEvent();
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  int sockets[2]{};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);

  pair.Client = pair.CreateBIOSocket();
  ASSERT_TRUE(pair.Client->Attach(sockets[0]));
  ASSERT_TRUE(pair.Client->SwitchToUnblockingMode());
  ASSERT_TRUE(pair.Client->TxQueue.Append("X", 1));

  pair.Client->TxLock.lock();
  bool flushed = true;
  const auto start = std::chrono::steady_clock::now();
  std::thread worker([&]() { flushed = pair.Client->Flush(40); });
  worker.join();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - start
  ).count();
  pair.Client->TxLock.unlock();

  EXPECT_FALSE(flushed);
  EXPECT_GE(elapsed, 25);
  EXPECT_LT(elapsed, 1000);
  EXPECT_FALSE(pair.Client->TxQueue.IsEmpty());

  pair.Close(SocketPairCloseMode::Fast);
  close(sockets[1]);
  CloseHandle(exitEvent);
}

TEST(Sockets, graceful_close_flushes_queued_data)
{
  Logme::ID ch = CH;
  HEvent exitEvent = CreateNotificationEvent();
  SocketPair pair(ch, exitEvent, std::make_shared<Config>());

  int sockets[2]{};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);

  pair.Client = pair.CreateBIOSocket();
  ASSERT_TRUE(pair.Client->Attach(sockets[0]));
  ASSERT_TRUE(pair.Client->SwitchToUnblockingMode());
  ASSERT_TRUE(pair.Client->TxQueue.Append("Hello", 5));

  pair.Close(SocketPairCloseMode::Graceful);
  EXPECT_TRUE(pair.ClosePending);
  EXPECT_TRUE(pair.Client->TxQueue.IsEmpty());

  char buffer[16]{};
  EXPECT_EQ(recv(sockets[1], buffer, sizeof(buffer), MSG_DONTWAIT), 5);
  EXPECT_EQ(std::string(buffer, 5), "Hello");

  close(sockets[1]);
  CloseHandle(exitEvent);
}
#endif
