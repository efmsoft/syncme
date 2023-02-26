#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/ProcessThreadId.h>
#include <Syncme/SetThreadName.h>
#include <Syncme/Sync.h>
#include <Syncme/ThreadPool/Worker.h>

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
  , Exited(false)
  , NotifyIdle(notifyIdle)
  , OnTimer(onTimer)
{
}

Worker::~Worker()
{
  Stop();

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
  assert(rc == WAIT_RESULT::OBJECT_0);

  return true;
}

void Worker::Stop()
{
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
}

HEvent Worker::Invoke(TCallback cb, uint64_t& id)
{
  assert(IsExpired() == false);
  assert(WaitForSingleObject(BusyEvent, 0) == WAIT_RESULT::TIMEOUT);
  assert(WaitForSingleObject(IdleEvent, 0) == WAIT_RESULT::OBJECT_0);
  assert(Thread);

  if (!Thread)
    return nullptr;

  id = ThreadID;

  HEvent h = Handle();
  if (h == nullptr)
    return nullptr;

  Callback = cb;

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
      assert(!"!?!?!?!");
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

    NotifyIdle(this);
  }

  Exited = true;
}
