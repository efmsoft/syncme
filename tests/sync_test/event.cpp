#include <stdio.h>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <Syncme/Sync.h>
#include <Syncme/TimePoint.h>
#include <Syncme/Timer/Counter.h>

using namespace Syncme;
using namespace std::chrono_literals;

const int THREADS = 100;

void setevent(HEvent event)
{
  std::this_thread::sleep_for(200 * 1ms);
  SetEvent(event);
}

TEST(Sync, wait_500)
{
  TimePoint t1;

  HEvent event = CreateNotificationEvent();
  std::jthread t(setevent, event);

  auto rc = WaitForSingleObject(event, 500);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);
  
  auto elapsed = t1.ElapsedSince();
  EXPECT_GE(elapsed, 200);
}

TEST(Sync, wait_timeout)
{
  TimePoint t1;

  HEvent event = CreateNotificationEvent();
  std::jthread t(setevent, event);

  auto rc = WaitForSingleObject(event, 100);
  EXPECT_EQ(rc, WAIT_RESULT::TIMEOUT);

  auto elapsed = t1.ElapsedSince();
  EXPECT_GE(elapsed, 100);
}

TEST(Sync, concurrency)
{
  HEvent event = CreateNotificationEvent();
  HEvent eventSync = CreateSynchronizationEvent();
  std::atomic<int> count{0};

  auto worker = [](
    HEvent evStart
    , HEvent evSync
    , std::atomic<int>& count
    , int index
  ) {
    WaitForSingleObject(evStart);
    if (WaitForSingleObject(evSync, 200) == WAIT_RESULT::OBJECT_0)
    {
      ++count;
      printf("thread %i notified\n", index);
    }
  };

  std::vector<std::thread*> threads;
  for (int i = 0; i < THREADS; i++)
  {
    threads.push_back(
      new std::thread(
        worker
        , event
        , eventSync
        , std::ref(count)
        , i
      )
    );
  }

  SetEvent(event);
  SetEvent(eventSync);

  for (auto t : threads)
  {
    t->join();
    delete t;
  }

  EXPECT_EQ(count, 1);
}

TEST(Sync, already_signalled)
{
  HEvent event = CreateNotificationEvent(STATE::SIGNALLED);
  
  auto rc = WaitForSingleObject(event, 0);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);
}

TEST(Sync, wait_for_single_object)
{
  TimePoint t1;
  HEvent event1 = CreateNotificationEvent();
  std::jthread t(setevent, event1);

  auto rc = WaitForSingleObject(event1, 500);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);

  auto elapsed = t1.ElapsedSince();
  EXPECT_GE(elapsed, 200);
}

TEST(Sync, wait_for_multiple_objects_all)
{
  HEvent event1 = CreateNotificationEvent();
  HEvent event2 = CreateSynchronizationEvent();
  HEvent event3 = CreateNotificationEvent();

  std::jthread t1(setevent, event1);
  std::jthread t2(setevent, event2);
  std::jthread t3(setevent, event3);
  
  EventArray ev(event1, event2, event3);
  auto rc = WaitForMultipleObjects(ev, true, FOREVER);
  bool f = rc >= WAIT_RESULT::OBJECT_0 && rc <= WAIT_RESULT::OBJECT_2;
  EXPECT_EQ(f, true);

  rc = WaitForSingleObject(event1, 0);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);

  // event2 is a synchronizatino event. it must be reset 
  // after completing WaitForMultipleObjects
  rc = WaitForSingleObject(event2, 0);
  EXPECT_EQ(rc, WAIT_RESULT::TIMEOUT);

  rc = WaitForSingleObject(event3, 0);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);
}

TEST(Sync, wait_for_multiple_objects_any)
{
  HEvent event1 = CreateNotificationEvent();
  HEvent event2 = CreateNotificationEvent();
  HEvent event3 = CreateNotificationEvent();

  std::jthread t2(setevent, event2);

  EventArray ev(event1, event2, event3);
  auto rc = WaitForMultipleObjects(ev, false);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_1);
}

TEST(Sync, duplicate_event)
{
  HEvent ev1 = CreateNotificationEvent();
  HEvent ev2 = DuplicateHandle(ev1);

  SetEvent(ev2);

  auto rc = WaitForSingleObject(ev1, 0);
  EXPECT_EQ(rc, WAIT_RESULT::OBJECT_0);

  ResetEvent(ev1);

  rc = WaitForSingleObject(ev2, 0);
  EXPECT_EQ(rc, WAIT_RESULT::TIMEOUT);

  CloseHandle(ev1);
  CloseHandle(ev2);
}

TEST(Sync, duplicate_sync_event)
{
  HEvent ev1 = CreateSynchronizationEvent();
  HEvent ev2 = DuplicateHandle(ev1);

  std::jthread t(setevent, ev1);

  EventArray ev(ev1, ev2);
  auto rc = WaitForMultipleObjects(ev, true, 1000);
  EXPECT_EQ(rc, WAIT_RESULT::TIMEOUT);
}

TEST(Sync, duplicate_notif_event)
{
  HEvent ev1 = CreateNotificationEvent();
  HEvent ev2 = DuplicateHandle(ev1);

  std::jthread t(setevent, ev1);

  EventArray ev(ev1, ev2);
  auto rc = WaitForMultipleObjects(ev, true, FOREVER);
  bool f = rc >= WAIT_RESULT::OBJECT_0 && rc <= WAIT_RESULT::OBJECT_1;
  EXPECT_EQ(f, true);
}

TEST(Sync, close_waiting_event_single)
{
  HEvent ev1 = CreateNotificationEvent();
  HEvent ev2 = CreateSynchronizationEvent();

  auto code = [](HEvent& ev) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    CloseHandle(ev);
  };

  std::jthread t1(code, std::ref(ev1));
  auto rc = WaitForSingleObject(ev1);
  EXPECT_EQ(rc, WAIT_RESULT::FAILED);

  std::jthread t2(code, std::ref(ev2));
  rc = WaitForSingleObject(ev2);
  EXPECT_EQ(rc, WAIT_RESULT::FAILED);
}

TEST(Sync, close_waiting_event_multiple)
{
  HEvent ev1 = CreateNotificationEvent();
  HEvent ev2 = CreateSynchronizationEvent();
  auto code = [](HEvent& ev) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    CloseHandle(ev);
  };

  std::jthread t(code, std::ref(ev1));

  EventArray object(ev1, ev2);
  auto rc = WaitForMultipleObjects(object, true);
  EXPECT_EQ(rc, WAIT_RESULT::FAILED);
}

TEST(Sync, mupltiple_signalled)
{
  HEvent e1 = CreateNotificationEvent(STATE::SIGNALLED);
  HEvent e2 = CreateSynchronizationEvent(STATE::SIGNALLED);

  EventArray arr(e1, e2);

  auto r = WaitForMultipleObjects(arr, false, 1000);
  bool f = r == WAIT_RESULT::OBJECT_0;
  EXPECT_EQ(f, true);

  ResetEvent(e1);

  r = WaitForMultipleObjects(arr, false, 1000);
  f = r == WAIT_RESULT::OBJECT_1;
  EXPECT_EQ(f, true);
}