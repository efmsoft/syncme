#include <gtest/gtest.h>
#include <Syncme/Timer/Counter.h>
#include <Syncme/Sync.h>

using namespace Syncme;

TEST(Sync, waitable_timer)
{
  HEvent timer = CreateManualResetTimer();
  EXPECT_EQ(TimerObjects, 1);

  bool f = SetWaitableTimer(timer, 1500, 0, [](HEvent t) { printf("callback called\n"); });
  EXPECT_EQ(f, true);
  EXPECT_EQ(QueuedTimers == 1 || GetEventState(timer) == STATE::SIGNALLED, true);
  EXPECT_EQ(TimerObjects, 1);

  auto rc = WaitForSingleObject(timer, FOREVER);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);
  EXPECT_EQ(QueuedTimers, 0);
  EXPECT_EQ(TimerObjects, 1);

  // It is manualReset timer. It should be signalled
  rc = WaitForSingleObject(timer, 0);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);

  CloseHandle(timer);
  EXPECT_EQ(TimerObjects, 0);
}

TEST(Sync, waitable_timer_2)
{
  HEvent timer = CreateAutoResetTimer();
  EXPECT_EQ(TimerObjects, 1);

  bool f = SetWaitableTimer(timer, 1500, 0, [](HEvent t) { printf("callback called\n"); });
  EXPECT_EQ(f, true);
  EXPECT_EQ(QueuedTimers, 1);
  EXPECT_EQ(TimerObjects, 1);

  auto rc = WaitForSingleObject(timer, FOREVER);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);
  EXPECT_EQ(QueuedTimers, 0);
  EXPECT_EQ(TimerObjects, 1);

  // It is not manualReset timer. It should be not signalled
  rc = WaitForSingleObject(timer, 0);
  EXPECT_EQ(rc, WAIT_RESULT::TIMEOUT);

  CloseHandle(timer);
  EXPECT_EQ(TimerObjects, 0);
}

TEST(Sync, waitable_timer_3)
{
  HEvent timer = CreateAutoResetTimer();
  EXPECT_EQ(TimerObjects, 1);

  bool f = SetWaitableTimer(timer, 1500, 1500, nullptr);
  EXPECT_EQ(f, true);
  EXPECT_EQ(QueuedTimers, 1);
  EXPECT_EQ(TimerObjects, 1);

  auto rc = WaitForSingleObject(timer, FOREVER);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);
  EXPECT_EQ(TimerObjects, 1);

  rc = WaitForSingleObject(timer, 0);
  EXPECT_EQ(rc, WAIT_RESULT::TIMEOUT);

  rc = WaitForSingleObject(timer, FOREVER);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);

  f = CancelWaitableTimer(timer);
  EXPECT_EQ(f, true);
  EXPECT_EQ(QueuedTimers, 0);

  CloseHandle(timer);
  EXPECT_EQ(TimerObjects, 0);
}

TEST(Sync, waitable_timer_cancel_before_signal)
{
  HEvent timer = CreateAutoResetTimer();
  ASSERT_TRUE(timer);

  bool f = SetWaitableTimer(timer, 500, 0, nullptr);
  ASSERT_TRUE(f);

  f = CancelWaitableTimer(timer);
  EXPECT_TRUE(f);
  EXPECT_EQ(WaitForSingleObject(timer, 100), WAIT_RESULT::TIMEOUT);

  CloseHandle(timer);
  EXPECT_EQ(TimerObjects, 0);
}

TEST(Sync, waitable_timer_close_pending)
{
  HEvent timer = CreateManualResetTimer();
  ASSERT_TRUE(timer);

  bool f = SetWaitableTimer(timer, 5000, 0, nullptr);
  ASSERT_TRUE(f);

  EXPECT_TRUE(CloseHandle(timer));
  EXPECT_EQ(TimerObjects, 0);
  EXPECT_EQ(QueuedTimers, 0);
}
TEST(Sync, waitable_timer_duplicate)
{
  HEvent timer = CreateManualResetTimer();
  HEvent duplicate = DuplicateHandle(timer);

  ASSERT_TRUE(timer);
  ASSERT_TRUE(duplicate);

  bool f = SetWaitableTimer(timer, 20, 0, nullptr);
  ASSERT_TRUE(f);

  EXPECT_EQ(WaitForSingleObject(duplicate, 1000), WAIT_RESULT::OBJECT_0);
  EXPECT_TRUE(CloseHandle(timer));

  EXPECT_FALSE(GetEventClosed(duplicate));
  EXPECT_EQ(WaitForSingleObject(duplicate, 0), WAIT_RESULT::OBJECT_0);

  CloseHandle(duplicate);
  EXPECT_EQ(TimerObjects, 0);
}
