#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <Syncme/Sync.h>
#include <Syncme/ThreadPool/Pool.h>
#include <Syncme/ThreadPool/Counter.h>

using namespace Syncme;
using namespace Syncme::ThreadPool;

static void WaitForIdleWorkers()
{
  for (int i = 0; i < 5000; ++i)
  {
    if (GetThreadsTotal() == GetThreadsUnused())
      return;

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

TEST(Pool, TryRunRejectsWithoutQueuingWhenWaitPoolIsFull)
{
  Pool pool;
  pool.SetMaxThreads(1);
  pool.SetMaxUnusedThreads(1);
  pool.SetOverflowMode(OVERFLOW_MODE::WAIT);

  HEvent started = CreateNotificationEvent();
  HEvent release = CreateNotificationEvent();
  std::atomic<int> rejectedExecuted(0);

  HEvent active = pool.Run([started, release]()
  {
    SetEvent(started);
    WaitForSingleObject(release);
  });

  ASSERT_TRUE(active);
  ASSERT_EQ(WaitForSingleObject(started, 5000), WAIT_RESULT::OBJECT_0);

  auto begin = std::chrono::steady_clock::now();
  for (int i = 0; i < 100; ++i)
  {
    auto owner = std::make_shared<int>(i);
    std::weak_ptr<int> weak = owner;
    uint64_t id = 123;

    HEvent rejected = pool.TryRun([owner, &rejectedExecuted]()
    {
      rejectedExecuted++;
    }, &id);

    EXPECT_FALSE(rejected);
    EXPECT_EQ(id, 0u);
    owner.reset();
    EXPECT_TRUE(weak.expired());
  }
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - begin
  ).count();

  EXPECT_LT(elapsed, 2000);

  SetEvent(release);
  EXPECT_EQ(WaitForSingleObject(active, 5000), WAIT_RESULT::OBJECT_0);

  HEvent resumed = pool.Run([]() {});
  EXPECT_TRUE(resumed);
  if (resumed)
    EXPECT_EQ(WaitForSingleObject(resumed, 5000), WAIT_RESULT::OBJECT_0);

  EXPECT_EQ(rejectedExecuted.load(), 0);

  if (resumed)
    CloseHandle(resumed);
  CloseHandle(active);
  CloseHandle(started);
  CloseHandle(release);
  WaitForIdleWorkers();
  pool.Stop();
}

TEST(Pool, TryRunSchedulesWhenWorkerIsAvailable)
{
  Pool pool;
  pool.SetMaxThreads(1);
  pool.SetMaxUnusedThreads(1);

  HEvent callbackFinished = CreateNotificationEvent();
  HEvent handle = pool.TryRun([callbackFinished]()
  {
    SetEvent(callbackFinished);
  });

  ASSERT_TRUE(handle);
  EXPECT_EQ(WaitForSingleObject(callbackFinished, 5000), WAIT_RESULT::OBJECT_0);
  EXPECT_EQ(WaitForSingleObject(handle, 5000), WAIT_RESULT::OBJECT_0);

  CloseHandle(handle);
  CloseHandle(callbackFinished);
  WaitForIdleWorkers();
  pool.Stop();
}

TEST(Pool, TryRunDoesNotStealWaitingRunTask)
{
  Pool pool;
  pool.SetMaxThreads(1);
  pool.SetMaxUnusedThreads(1);
  pool.SetOverflowMode(OVERFLOW_MODE::WAIT);

  HEvent started = CreateNotificationEvent();
  HEvent release = CreateNotificationEvent();

  HEvent first = pool.Run([started, release]()
  {
    SetEvent(started);
    WaitForSingleObject(release);
  });
  ASSERT_TRUE(first);
  ASSERT_EQ(WaitForSingleObject(started, 5000), WAIT_RESULT::OBJECT_0);

  std::promise<HEvent> result;
  auto future = result.get_future();
  std::thread submitter([&pool, &result]()
  {
    result.set_value(pool.Run([]() {}));
  });

  EXPECT_EQ(future.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);

  HEvent rejected = pool.TryRun([]() {});
  EXPECT_FALSE(rejected);

  SetEvent(release);
  EXPECT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);

  HEvent second = future.get();
  EXPECT_TRUE(second);
  if (second)
    EXPECT_EQ(WaitForSingleObject(second, 5000), WAIT_RESULT::OBJECT_0);

  submitter.join();
  if (second)
    CloseHandle(second);
  CloseHandle(first);
  CloseHandle(started);
  CloseHandle(release);
  WaitForIdleWorkers();
  pool.Stop();
}

TEST(Pool, ConcurrentTryRunHonorsMaxThreads)
{
  const int ITERATIONS = 20;
  const int SUBMITTERS = 12;

  for (int i = 0; i < ITERATIONS; ++i)
  {
    Pool pool;
    pool.SetMaxThreads(2);
    pool.SetMaxUnusedThreads(2);
    pool.SetOverflowMode(OVERFLOW_MODE::WAIT);

    HEvent release = CreateNotificationEvent();
    std::atomic<int> ready(0);
    std::atomic<bool> go(false);
    std::vector<HEvent> handles(SUBMITTERS);
    std::vector<std::thread> submitters;

    for (int j = 0; j < SUBMITTERS; ++j)
    {
      submitters.emplace_back([&, j]()
      {
        ready++;
        while (!go.load())
          std::this_thread::yield();

        handles[j] = pool.TryRun([release]()
        {
          WaitForSingleObject(release);
        });
      });
    }

    while (ready.load() < SUBMITTERS)
      std::this_thread::yield();
    go = true;

    for (auto& thread : submitters)
      thread.join();

    size_t scheduled = 0;
    for (auto& handle : handles)
      if (handle)
        scheduled++;

    EXPECT_LE(scheduled, 2u);

    SetEvent(release);
    for (auto& handle : handles)
    {
      if (!handle)
        continue;

      EXPECT_EQ(WaitForSingleObject(handle, 5000), WAIT_RESULT::OBJECT_0);
      CloseHandle(handle);
    }

    CloseHandle(release);
    WaitForIdleWorkers();
    pool.Stop();
  }
}

TEST(Pool, TryRunRejectsAfterPoolStops)
{
  Pool pool;
  pool.Stop();

  bool ran = false;
  uint64_t id = 123;
  HEvent handle = pool.TryRun([&ran]()
  {
    ran = true;
  }, &id);

  EXPECT_FALSE(handle);
  EXPECT_EQ(id, 0u);
  EXPECT_FALSE(ran);
}
