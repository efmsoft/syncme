#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <Syncme/Sleep.h>
#include <Syncme/Sync.h>
#include <Syncme/ThreadPool/Pool.h>
#include <Syncme/TickCount.h>

using namespace Syncme;
using namespace Syncme::ThreadPool;

static std::mutex Lock;
static std::vector<uint32_t> Values;

class Object
{
  HEvent EvStop;
  HEvent EvAbort;
  HEvent Thread;

  uint32_t Cookie;

public:
  Object(HEvent stopEvent, Pool& pool, uint32_t cookie)
    : EvStop(stopEvent)
    , EvAbort(CreateNotificationEvent())
    , Cookie(cookie)
  {
    Thread = pool.Run(std::bind(&Object::Worker, this));
  }

  ~Object()
  {
    if (Thread)
    {
      SetEvent(EvAbort);

      auto rc = WaitForSingleObject(Thread);
      assert(rc == WAIT_RESULT::OBJECT_0);
    }
  }

  void Worker()
  {
    unsigned ms = std::rand() % 3000;
    
    EventArray ev(EvStop, EvAbort);
    auto rc = WaitForMultipleObjects(ev, false, ms);

    if (true)
    {
      std::lock_guard<std::mutex> guard(Lock);
      Values.push_back(Cookie);
    }

    ms = std::rand() % 10000;
    rc = WaitForMultipleObjects(ev, false, ms);
  }
};

typedef std::shared_ptr<Object> ObjectPtr;
typedef std::vector<ObjectPtr> ObjectArray;

TEST(Pool, complex)
{
  std::srand((unsigned)GetTimeInMillisec());

  Values.clear();

  uint32_t cookie = 1;
  HEvent evStop = CreateNotificationEvent();

  Pool tpool;
  ObjectArray ob;
  for (int i = 0; i < 100; i++)
  {
    int ms = std::rand() % 200;
    Sleep(ms);

    ObjectPtr o = std::make_shared<Object>(evStop, tpool, cookie++);
    ob.push_back(o);
  }

  SetEvent(evStop);
  ob.clear();
  tpool.Stop();

  std::lock_guard<std::mutex> guard(Lock);
  std::sort(Values.begin(), Values.end());

  uint32_t c = 1;
  for (auto v : Values)
  {
    EXPECT_EQ(v, c++);
  }
}