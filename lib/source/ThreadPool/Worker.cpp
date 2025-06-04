#include <cassert>
#include <random>

#include <Syncme/Logger/Log.h>
#include <Syncme/ProcessThreadId.h>
#include <Syncme/SetThreadName.h>
#include <Syncme/Sync.h>
#include <Syncme/ThreadPool/Counter.h>
#include <Syncme/ThreadPool/Worker.h>
#include <Syncme/TimePoint.h>
#include <Syncme/TickCount.h>

#ifdef _WIN32
#include <windows.h>
#endif

#pragma warning(disable : 4996)

using namespace Syncme::ThreadPool;

namespace Syncme::ThreadPool
{
  std::atomic<uint64_t> WorkersDescructed;
}
uint64_t Syncme::ThreadPool::GetWorkersDescructed() {return WorkersDescructed;}

Worker::Worker(
  HEvent managementTimer
  , TOnIdle notifyIdle
  , TOnTimer onTimer
)
  : ManagementTimer(managementTimer)
  , StartedEvent(CreateNotificationEvent())
  , StopEvent(CreateNotificationEvent())
  , IdleEvent(CreateNotificationEvent())
  , BusyEvent(CreateSynchronizationEvent())
  , InvokeEvent(CreateNotificationEvent())
  , ExpireTimer(CreateManualResetTimer())
  , ThreadID{}
  , Started(false)
  , Stopped(false)
  , Exited(false)
  , NotifyIdle(notifyIdle)
  , OnTimer(onTimer)
{
}

Worker::~Worker()
{
  assert(Stopped == true);

  CloseHandle(StartedEvent);
  CloseHandle(StopEvent);
  CloseHandle(IdleEvent);
  CloseHandle(BusyEvent);
  CloseHandle(InvokeEvent);
  CloseHandle(ExpireTimer);

  WorkersDescructed++;
}

WorkerPtr Worker::Get() 
{
  return shared_from_this();
}

HEvent Worker::Handle()
{
  HEvent h = DuplicateHandle(IdleEvent);
  return h;
}

uint64_t Worker::GetTid() const
{
  return ThreadID;
}

void Worker::SetExpireTimer(long ms)
{
  if (ms)
    SetWaitableTimer(ExpireTimer, ms, 0, nullptr);
  else
    SetEvent(ExpireTimer);
}

void Worker::CancelExpireTimer()
{
  CancelWaitableTimer(ExpireTimer);
  ResetEvent(ExpireTimer);
}

bool Worker::IsExpired() const
{
  return GetEventState(ExpireTimer) == STATE::SIGNALLED;
}

HEvent Worker::Start(TCallback cb, uint64_t* id)
{
  assert(Exited == false);
  assert(Stopped == false);
  assert(Started == false);

  assert(Thread == nullptr);

  if (Thread)
    return HEvent();

  if (!StopEvent || !IdleEvent || !BusyEvent || !InvokeEvent || !ExpireTimer)
    return HEvent();

  HEvent h = Handle();
  if (h == nullptr)
    return HEvent();

  if (true)
  {
    auto guard = StateLock.Lock();

    Callback = cb;

    Thread = std::make_shared<std::thread>(&Worker::EntryPoint, this);
    if (Thread == nullptr)
    {
      LogE("Unable to start thread");

      Callback = TCallback();
      return HEvent();
    }

    Started = true;
  }

  if (id)
  {
#ifdef _WIN32
    *id = GetThreadId((HANDLE)Thread->native_handle());
#else
    WaitForSingleObject(StartedEvent);
    *id = ThreadID;
#endif
  }
  
  return h;
}

void Worker::Stop()
{
  assert(Started == true);
  assert(Stopped == false);

  if (Thread)
  {
    SetEvent(StopEvent);

    // Do not wait for thread termination if object is deleted from OnFree()
    auto id = GetCurrentThreadId();

    assert(ThreadID);
    assert(id != ThreadID);

    if (id != ThreadID)
      Thread->join();

    Thread.reset();
    ThreadID = 0;
  }

  Stopped = true;
}

HEvent Worker::Invoke(TCallback cb, uint64_t& id)
{
  assert(Started == true);
  assert(Stopped == false);
  assert(Exited == false);

  auto stateExpired = GetEventState(ExpireTimer);
  auto stateBusy = GetEventState(BusyEvent);
  auto stateIdle = GetEventState(IdleEvent);
  auto stateInvoke = GetEventState(InvokeEvent);

  assert(stateExpired == STATE::NOT_SIGNALLED);
  assert(stateBusy == STATE::NOT_SIGNALLED);
  assert(stateIdle == STATE::SIGNALLED);
  assert(stateInvoke == STATE::NOT_SIGNALLED);
  assert(Thread);

  if (!Thread)
    return nullptr;

  id = ThreadID;

  HEvent h = Handle();
  if (h == nullptr)
    return nullptr;

  Callback = cb;

  // Acquire StateLock to prevent NotifyIdle() called till WaitForMultipleObjects() completion
  auto guard = StateLock.Lock();

  ResetEvent(IdleEvent);
  ResetEvent(BusyEvent);

  SetEvent(InvokeEvent);

  EventArray object(StopEvent, BusyEvent);

  auto rc = WaitForMultipleObjects(object, false, FOREVER);
  assert(rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1);

  return h;
}

void Worker::EntryPoint()
{
  char name[64];
  sprintf(name, "TPool:%p", this);

  ThreadID = GetCurrentThreadId();
  SetEvent(StartedEvent);

  SET_CUR_THREAD_NAME(name);
  assert(Exited == false);

  EventArray object(StopEvent, InvokeEvent, ManagementTimer);

  bool firstTask = true;
  WAIT_RESULT rc = WAIT_RESULT::OBJECT_1;
  goto OnInvoke;

  for (;;)
  {
    firstTask = false;

    rc = WaitForMultipleObjects(object, false, FOREVER);
    if (rc == WAIT_RESULT::OBJECT_0)
      break;

    if (rc == WAIT_RESULT::OBJECT_2)
    {
      OnTimer(this);
      continue;
    }

    if (rc != WAIT_RESULT::OBJECT_1)
    {
      assert(!"!?!?!?!");
      break;
    }

  OnInvoke:
    ResetEvent(InvokeEvent);
    
    if (firstTask == false)
      SetEvent(BusyEvent);

    Callback();

    // We use IdleEvent to emulate thread handle. Clients can use it to 
    // check that thread is exited. So we return duplicated handle from Handle(),
    // signal event here. We have to create new event to work with new client
    SetEvent(IdleEvent);
    CloseHandle(IdleEvent);
    
    IdleEvent = CreateNotificationEvent(STATE::SIGNALLED);
    if (IdleEvent == nullptr)
    {
      LogE("IdleEvent: CreateCommonEvent failed");
      break;
    }

    SET_CUR_THREAD_NAME(name);

    auto guard = StateLock.Lock();
    TaskPtr task = NotifyIdle(this);
    
    if (task)
    {
      Callback = task->Callback;
      firstTask = true;
      
      guard.Release();
      goto OnInvoke;
    }
  }

  Exited = true;
}
