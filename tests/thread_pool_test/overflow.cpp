#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>
#include <Syncme/Sync.h>
#include <Syncme/ThreadPool/Counter.h>
#include <Syncme/ThreadPool/Pool.h>

using namespace Syncme;
using namespace Syncme::ThreadPool;

TEST(Pool, fail_overflow_releases_rejected_tasks)
{
  const int REJECTED_TASKS = 100;

  Pool tpool;
  tpool.SetMaxThreads(1);
  tpool.SetMaxUnusedThreads(1);
  tpool.SetOverflowMode(OVERFLOW_MODE::FAIL);

  HEvent started = CreateNotificationEvent();
  HEvent release = CreateNotificationEvent();
  std::atomic<int> unexpectedExecutions{};

  HEvent active = tpool.Run(
    [started, release]()
    {
      SetEvent(started);
      WaitForSingleObject(release);
    }
  );

  ASSERT_TRUE(active);
  ASSERT_EQ(WaitForSingleObject(started, 5000), WAIT_RESULT::OBJECT_0);

  for (int i = 0; i < REJECTED_TASKS; ++i)
  {
    std::weak_ptr<int> weak;
    HEvent rejected;

    {
      auto resource = std::make_shared<int>(i);
      weak = resource;
      rejected = tpool.Run(
        [resource, &unexpectedExecutions]()
        {
          unexpectedExecutions++;
        }
      );
    }

    EXPECT_FALSE(rejected);
    EXPECT_TRUE(weak.expired()) << "Rejected task " << i;
  }

  SetEvent(release);
  EXPECT_EQ(WaitForSingleObject(active, 5000), WAIT_RESULT::OBJECT_0);

  for (int i = 0; i < 1000 && GetThreadsUnused() != 1; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

  EXPECT_EQ(unexpectedExecutions.load(), 0);
  EXPECT_EQ(GetThreadsUnused(), 1);

  CloseHandle(active);
  CloseHandle(started);
  CloseHandle(release);
  tpool.Stop();
}
