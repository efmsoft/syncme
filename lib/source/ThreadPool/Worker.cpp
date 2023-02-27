#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/ProcessThreadId.h>
#include <Syncme/SetThreadName.h>
#include <Syncme/Sync.h>
#include <Syncme/ThreadPool/Worker.h>

#include <windows.h>
#include <debugapi.h>

#define _assert(expr) \
  do { \
      if (!(expr)) \
          DebugBreak(); \
  } while (0)

#pragma warning(disable : 4996)

using namespace Syncme::ThreadPool;

namespace Syncme::ThreadPool
{
  std::atomic<uint64_t> WorkersDescructed;
}

Worker::Worker(
  HEvent managementTimer
  , TOnIdle notifyIdle
  , TOnTimer onTimer
)
  : ManagementTimer(managementTimer)
  , StopEvent(CreateNotificationEvent())
  , IdleEvent(CreateNotificationEvent())
  , BusyEvent(CreateSynchronizationEvent())
  , InvokeEvent(CreateSynchronizationEvent())
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
  _assert(Stopped == true);

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

bool Worker::Start()
{
  _assert(Exited == false);
  _assert(Stopped == false);
  _assert(Started == false);

  _assert(Thread == nullptr);

  if (Thread)
    return true;

  if (!StopEvent || !IdleEvent || !BusyEvent || !InvokeEvent || !ExpireTimer)
    return false;

  Thread = std::make_shared<std::thread>(&Worker::EntryPoint, this);
  if (Thread == nullptr)
  {
    LogE("Unable to start thread");
    return false;
  }

  auto rc = WaitForSingleObject(IdleEvent, FOREVER);
  _assert(rc == WAIT_RESULT::OBJECT_0);

  Started = true;
  return true;
}

void Worker::Stop()
{
  _assert(Started == true);
  _assert(Stopped == false);

  if (Thread)
  {
    SetEvent(StopEvent);

    // Do not wait for thread termination if object is deleted from OnFree()
    auto id = GetCurrentThreadId();

    _assert(ThreadID);
    _assert(id != ThreadID);

    if (id != ThreadID)
      Thread->join();

    Thread.reset();
    ThreadID = 0;
  }

  Stopped = true;
}

HEvent Worker::Invoke(TCallback cb, uint64_t& id)
{
  _assert(Started == true);
  _assert(Stopped == false);
  _assert(Exited == false);

  auto stateExpired = GetEventState(ExpireTimer);
  auto stateBusy = GetEventState(BusyEvent);
  auto stateIdle = GetEventState(IdleEvent);

  _assert(stateExpired == STATE::NOT_SIGNALLED);
  _assert(stateBusy == STATE::NOT_SIGNALLED);
  _assert(stateIdle == STATE::SIGNALLED);
  _assert(Thread);

  if (!Thread)
    return nullptr;

  id = ThreadID;

  HEvent h = Handle();
  if (h == nullptr)
    return nullptr;

  Callback = cb;

  ResetEvent(IdleEvent);
  ResetEvent(BusyEvent);

  // Acquire StateLock to prevent NotifyIdle() called till WaitForMultipleObjects() completion
  auto guard = StateLock.Lock();
  
  SetEvent(InvokeEvent);

  EventArray object(StopEvent, BusyEvent);
  auto rc = WaitForMultipleObjects(object, false, FOREVER);
  _assert(rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1);

  return h;
}

void Worker::EntryPoint()
{
  char name[64];
  sprintf(name, "TPool:%p", this);
  ThreadID = GetCurrentThreadId();

  _assert(Exited == false);

  EventArray object(StopEvent, InvokeEvent, ManagementTimer);
  SetEvent(IdleEvent);
  
  for (;;)
  {
    SET_CUR_THREAD_NAME(name);

    auto rc = WaitForMultipleObjects(object, false, FOREVER);
    if (rc == WAIT_RESULT::OBJECT_0)
      break;

    if (rc == WAIT_RESULT::OBJECT_2)
    {
      OnTimer(this);
      continue;
    }

    if (rc != WAIT_RESULT::OBJECT_1)
    {
      _assert(!"!?!?!?!");
      break;
    }

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

    auto guard = StateLock.Lock();
    NotifyIdle(this);
  }

  Exited = true;
}
