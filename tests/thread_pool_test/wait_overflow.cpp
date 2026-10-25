#include <chrono>
#include <future>
#include <thread>

#include <gtest/gtest.h>
#include <Syncme/Sync.h>
#include <Syncme/ThreadPool/Pool.h>

using namespace Syncme;
using namespace Syncme::ThreadPool;

TEST(Pool, WaitOverflowReturnsBeforeDispatchedTaskFinishes)
{
  const int ITERATIONS = 20;

  for (int i = 0; i < ITERATIONS; ++i)
  {
    Pool pool;
    pool.SetMaxThreads(1);
    pool.SetMaxUnusedThreads(1);
    pool.SetOverflowMode(OVERFLOW_MODE::WAIT);

    HEvent firstStarted = CreateNotificationEvent();
    HEvent firstRelease = CreateNotificationEvent();
    HEvent submitStarted = CreateNotificationEvent();
    HEvent secondRelease = CreateNotificationEvent();

    HEvent first = pool.Run([firstStarted, firstRelease]()
    {
      SetEvent(firstStarted);
      WaitForSingleObject(firstRelease);
    });

    EXPECT_TRUE(first);
    EXPECT_EQ(WaitForSingleObject(firstStarted, 5000), WAIT_RESULT::OBJECT_0);

    std::promise<HEvent> promise;
    auto submitted = promise.get_future();
    std::thread submitter([&pool, &promise, submitStarted, secondRelease]()
    {
      SetEvent(submitStarted);
      HEvent second = pool.Run([secondRelease]()
      {
        WaitForSingleObject(secondRelease);
      });
      promise.set_value(second);
    });

    EXPECT_EQ(WaitForSingleObject(submitStarted, 5000), WAIT_RESULT::OBJECT_0);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    SetEvent(firstRelease);
    EXPECT_EQ(WaitForSingleObject(first, 5000), WAIT_RESULT::OBJECT_0);

    // A submitted task is allowed to remain busy. Pool::Run() must still
    // return its handle without waiting for that task to finish.
    bool returned = submitted.wait_for(std::chrono::milliseconds(200))
      == std::future_status::ready;

    // Also release it on failure, so the regression test does not hang the
    // suite while reporting an unexpectedly blocked Pool::Run().
    SetEvent(secondRelease);
    HEvent second = submitted.get();
    EXPECT_TRUE(second);
    if (second)
      EXPECT_EQ(WaitForSingleObject(second, 5000), WAIT_RESULT::OBJECT_0);

    submitter.join();
    EXPECT_TRUE(returned) << "iteration=" << i;

    if (second)
      CloseHandle(second);
    CloseHandle(first);
    CloseHandle(firstStarted);
    CloseHandle(firstRelease);
    CloseHandle(submitStarted);
    CloseHandle(secondRelease);
    pool.Stop();
  }
}
