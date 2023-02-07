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