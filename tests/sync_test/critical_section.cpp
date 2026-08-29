#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <Syncme/CritSection.h>

using namespace Syncme;

TEST(Sync, critical_section_mutual_exclusion)
{
  const int THREAD_COUNT = 8;
  const int ITERATIONS = 10000;

  CS lock;
  int value = 0;

  std::vector<std::thread> threads;
  threads.reserve(THREAD_COUNT);

  for (int i = 0; i < THREAD_COUNT; ++i)
  {
    threads.emplace_back(
      [&lock, &value]()
      {
        for (int n = 0; n < ITERATIONS; ++n)
        {
          auto guard = lock.Lock();
          value++;
        }
      }
    );
  }

  for (auto& thread : threads)
    thread.join();

  EXPECT_EQ(value, THREAD_COUNT * ITERATIONS);
}

TEST(Sync, critical_section_try_lock)
{
  CS lock;
  std::atomic<bool> acquired{ true };

  auto guard = lock.Lock();

  std::thread thread(
    [&lock, &acquired]()
    {
      auto tryGuard = lock.TryLock();
      acquired = bool(tryGuard);
    }
  );

  thread.join();
  EXPECT_FALSE(acquired.load());
}

TEST(Sync, critical_section_recursive_lock)
{
  CS lock;

  auto first = lock.Lock();
  auto second = lock.TryLock();

  EXPECT_TRUE(bool(first));
  EXPECT_TRUE(bool(second));

  second.Release();
  first.Release();

  auto third = lock.TryLock();
  EXPECT_TRUE(bool(third));
}
