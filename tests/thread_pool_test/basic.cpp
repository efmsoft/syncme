#include <functional>

#include <gtest/gtest.h>
#include <Syncme/Sync.h>
#include <Syncme/ThreadPool/Pool.h>

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
  tpool.Stop();
}