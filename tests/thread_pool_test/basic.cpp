#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>
#include <Syncme/Sync.h>
#include <Syncme/ThreadPool/Pool.h>
#include <Syncme/ThreadPool/Counter.h>

using namespace Syncme;
using namespace Syncme::ThreadPool;

void thread1(HEvent event)
{
  SetEvent(event);
}

TEST(Pool, basic)
{
  Pool tpool;
  HEvent event = CreateNotificationEvent();

  uint64_t pid{};
  HEvent h = tpool.Run(std::bind(thread1, event), &pid);

  EventArray ea(event, h);
  auto rc = WaitForMultipleObjects(ea, true);
  bool f = rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1;

  EXPECT_EQ(f, true);
  CloseHandle(h);
  CloseHandle(event);
  tpool.Stop();
}

TEST(Pool, worker_handle_reuse)
{
  const int ITERATIONS = 500;

  Pool tpool;
  tpool.SetMaxThreads(1);
  tpool.SetMaxUnusedThreads(1);
  tpool.SetMaxIdleTime(10000);

  std::atomic<int> completed{};

  uint64_t firstId{};
  HEvent first = tpool.Run(
    [&completed]()
    {
      completed++;
    }
    , &firstId
  );

  ASSERT_TRUE(first);
  ASSERT_NE(firstId, 0);
  ASSERT_EQ(WaitForSingleObject(first, 5000), WAIT_RESULT::OBJECT_0);
  EXPECT_FALSE(GetEventClosed(first));

  for (int i = 0; i < ITERATIONS; ++i)
  {
    uint64_t id{};
    HEvent handle = tpool.Run(
      [&completed]()
      {
        completed++;
      }
      , &id
    );

    ASSERT_TRUE(handle);
    EXPECT_EQ(id, firstId);
    ASSERT_EQ(WaitForSingleObject(handle, 5000), WAIT_RESULT::OBJECT_0);
    EXPECT_FALSE(GetEventClosed(handle));
    EXPECT_TRUE(CloseHandle(handle));
  }

  EXPECT_EQ(completed.load(), ITERATIONS + 1);
  EXPECT_EQ(WaitForSingleObject(first, 0), WAIT_RESULT::OBJECT_0);

  CloseHandle(first);
  tpool.Stop();
}

TEST(Pool, worker_handle_close_while_running)
{
  const int ITERATIONS = 1000;

  Pool tpool;
  tpool.SetMaxThreads(8);

  std::atomic<int> completed{};
  HEvent allCompleted = CreateNotificationEvent();

  for (int i = 0; i < ITERATIONS; ++i)
  {
    HEvent handle = tpool.Run(
      [&completed, allCompleted]()
      {
        std::this_thread::yield();

        if (++completed == ITERATIONS)
          SetEvent(allCompleted);
      }
    );

    ASSERT_TRUE(handle);
    EXPECT_TRUE(CloseHandle(handle));
  }

  EXPECT_EQ(WaitForSingleObject(allCompleted, 10000), WAIT_RESULT::OBJECT_0);
  EXPECT_EQ(completed.load(), ITERATIONS);

  CloseHandle(allCompleted);
  tpool.Stop();
}

TEST(Pool, worker_releases_completed_callback)
{
  const int ITERATIONS = 100;

  Pool tpool;
  tpool.SetMaxThreads(1);
  tpool.SetMaxUnusedThreads(1);

  for (int i = 0; i < ITERATIONS; ++i)
  {
    std::weak_ptr<int> weak;
    HEvent handle;

    {
      auto resource = std::make_shared<int>(i);
      weak = resource;
      handle = tpool.Run([resource]() {});
    }

    ASSERT_TRUE(handle);
    ASSERT_EQ(WaitForSingleObject(handle, 5000), WAIT_RESULT::OBJECT_0);
    EXPECT_TRUE(weak.expired()) << "Iteration " << i;

    CloseHandle(handle);
  }

  for (int i = 0; i < 1000 && GetThreadsUnused() != 1; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

  ASSERT_EQ(GetThreadsUnused(), 1);
  tpool.Stop();
}

TEST(Pool, worker_releases_throwing_callback)
{
  Pool tpool;
  tpool.SetMaxThreads(1);
  tpool.SetMaxUnusedThreads(1);

  std::weak_ptr<int> weak;
  HEvent handle;

  {
    auto resource = std::make_shared<int>(1);
    weak = resource;
    handle = tpool.Run(
      [resource]()
      {
        throw std::runtime_error("expected test exception");
      }
    );
  }

  ASSERT_TRUE(handle);
  ASSERT_EQ(WaitForSingleObject(handle, 5000), WAIT_RESULT::OBJECT_0);
  EXPECT_TRUE(weak.expired());

  CloseHandle(handle);

  for (int i = 0; i < 1000 && GetThreadsUnused() != 1; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

  ASSERT_EQ(GetThreadsUnused(), 1);
  tpool.Stop();
}
