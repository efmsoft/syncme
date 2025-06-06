#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif

#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/ProcessThreadId.h>
#include <Syncme/Sleep.h>
#include <Syncme/ThreadPool/Counter.h>
#include <Syncme/ThreadPool/Pool.h>

#define LOCK_GUARD() \
  std::lock_guard<std::mutex> guard(Lock); \
  Owner = GetCurrentThreadId()

#define SET_TIMER() \
  SetWaitableTimer(Timer, 4 * MaxIdleTime / 3, 0, nullptr)

using namespace Syncme::ThreadPool;

static const size_t MAX_UNUSED_THREADS = 12;
static const size_t MAX_THREADS = 100;
static const long MAX_IDLE_TIME = 3000; // 3 sec
static const size_t COMPACT_PERCENT = 80;
static const size_t COMPACT_STEP = 10;

namespace Syncme::ThreadPool
{
  std::atomic<uint64_t> ThreadsTotal;
  std::atomic<uint64_t> ThreadsUnused;
  std::atomic<uint64_t> ThreadsStopped;
  std::atomic<uint64_t> LockedInRun;
  std::atomic<uint64_t> OnTimerCalls;
  std::atomic<uint64_t> CreateInvoke;
  std::atomic<uint64_t> DirectInvoke;
  std::atomic<uint64_t> SlowInvoke;
  std::atomic<uint64_t> Errors;

  std::atomic<uint64_t> LockedInRunCreateWorker;
  std::atomic<uint64_t> LockedInRunStop;
  std::atomic<uint64_t> LockedInRunFail;
  std::atomic<uint64_t> LockedInRunInvoke;
  std::atomic<uint64_t> LockedInRunInvokeError;
  std::atomic<uint64_t> LockedInCompact;
}

uint64_t Syncme::ThreadPool::GetThreadsTotal() {return ThreadsTotal;}
uint64_t Syncme::ThreadPool::GetThreadsUnused() {return ThreadsUnused;}
uint64_t Syncme::ThreadPool::GetThreadsStopped() {return ThreadsStopped;}
uint64_t Syncme::ThreadPool::GetLockedInRun() {return LockedInRun;}
uint64_t Syncme::ThreadPool::GetOnTimerCalls() {return OnTimerCalls;}
uint64_t Syncme::ThreadPool::GetErrors() {return Errors;}
uint64_t Syncme::ThreadPool::GetLockedInRunCreateWorker() { return LockedInRunCreateWorker; }
uint64_t Syncme::ThreadPool::GetLockedInRunStop() { return LockedInRunStop; }
uint64_t Syncme::ThreadPool::GetLockedInRunFail() { return LockedInRunFail; }
uint64_t Syncme::ThreadPool::GetLockedInRunInvoke() { return LockedInRunInvoke; }
uint64_t Syncme::ThreadPool::GetLockedInRunInvokeError() { return LockedInRunInvokeError; }
uint64_t Syncme::ThreadPool::GetLockedInCompact() { return LockedInCompact; }
uint64_t Syncme::ThreadPool::GetDirectInvoke() { return DirectInvoke; }
uint64_t Syncme::ThreadPool::GetSlowInvoke() { return SlowInvoke; }
uint64_t Syncme::ThreadPool::GetCreateInvoke() { return CreateInvoke; }

Pool::Pool()
  : MaxUnusedThreads(MAX_UNUSED_THREADS)
  , MaxThreads(MAX_THREADS)
  , MaxIdleTime(MAX_IDLE_TIME)
  , Mode(OVERFLOW_MODE::WAIT)
  , Timer(CreateAutoResetTimer())
  , Owner(0)
  , Stopping(false)
{
  FreeEvent = CreateSynchronizationEvent();
  StopEvent = CreateNotificationEvent();
}

Pool::~Pool()
{
  CloseHandle(StopEvent);
  CloseHandle(FreeEvent);
}

size_t Pool::GetMaxThread() const
{
  return MaxThreads;
}

void Pool::SetMaxThreads(size_t n)
{
  assert(n > 0);
  MaxThreads = n;
}

size_t Pool::GetMaxUnusedThreads() const
{
  return MaxUnusedThreads;
}

void Pool::SetMaxUnusedThreads(size_t n)
{
  MaxUnusedThreads = n;
}

long Pool::GetMaxIdleTime() const
{
  return MaxIdleTime;
}

void Pool::SetMaxIdleTime(long t)
{
  MaxIdleTime = t;
}

OVERFLOW_MODE Pool::GetOverflowMode() const
{
  return Mode;
}

void Pool::SetOverflowMode(OVERFLOW_MODE mode)
{
  Mode = mode;
}

void Pool::SetCompact(SCompact compact)
{
  Compact = compact;
}

void Pool::SetStopping()
{
  LOCK_GUARD();
  Stopping = true;
  SetEvent(StopEvent);
}

void Pool::Stop()
{
  SetStopping();

  for (auto& e : All)
  {
    e->Stop();
    ThreadsStopped++;
  }

  LOCK_GUARD();
  assert(All.size() == Unused.size());

  Unused.clear();
  ThreadsUnused = 0;

  All.clear();
  ThreadsTotal = 0;
}

void Pool::StopUnused()
{
  LOCK_GUARD();

  for (auto& e : Unused)
    e->SetExpireTimer(0);

  Locked_StopExpired(nullptr);
}

WorkerPtr Pool::PopUnused(size_t& allCount)
{
  LOCK_GUARD();

  allCount = All.size();

  if (Unused.empty())
    return WorkerPtr();

  WorkerPtr t = Unused.front();
  Unused.pop_front();
  
  ThreadsUnused = Unused.size();

  t->CancelExpireTimer();
  Locked_StopExpired(nullptr);
  
  return t;
}

void Pool::Push(WorkerList& list, WorkerPtr t)
{
  LOCK_GUARD();
  list.push_back(t);

  ThreadsUnused = Unused.size();
  ThreadsTotal = All.size();
}

WorkerPtr Pool::CreateWorker(
  const TimePoint& t0
  , TCallback cb
  , uint64_t* pid
  , HEvent& thread
)
{
  TOnIdle notifyIdle = std::bind(&Pool::CB_OnFree, this, std::placeholders::_1);
  TOnTimer onTimer = std::bind(&Pool::CB_OnTimer, this, std::placeholders::_1);
  WorkerPtr t = std::make_shared<Worker>(Timer, notifyIdle, onTimer);
  Push(All, t);

  thread = t->Start(cb, pid);
  if (!thread)
  {
    Errors++;

    LockedInRunCreateWorker += t0.ElapsedSince();
    LockedInRun += t0.ElapsedSince();

    LOCK_GUARD();

    auto ita = std::find_if(
      All.begin()
      , All.end()
      , [t](WorkerPtr e) { return e.get() == t.get(); }
    );
    
    if (ita != All.end())
      All.erase(ita);

    return nullptr;
  }

  return t;
}

void Pool::DoCompact()
{
  TimePoint t0;

  SCompact compact = Compact;
  if (compact)
  {
    size_t try2free = 0;

    if (true)
    {
      LOCK_GUARD();

      size_t inuse = All.size() - Unused.size();
      size_t limit = (100 * inuse) / MaxThreads;
      if (limit >= COMPACT_PERCENT)
      {
        size_t desired = (COMPACT_PERCENT - COMPACT_STEP) * MaxThreads / 100;
        try2free = inuse - desired;
      }
    }

    if (try2free)
      compact(try2free);
  }

  LockedInCompact += t0.ElapsedSince();
}

TaskPtr Pool::QueueTask(TCallback cb)
{
  TaskPtr task = std::make_shared<Task>();
  task->Callback = cb;

  std::lock_guard guard(TaskLock);
  Tasks.push_back(task);

  return task;
}

bool Pool::DequeueTask(TaskPtr task)
{
  std::lock_guard guard(TaskLock);

  auto it = std::find_if(
    Tasks.begin()
    , Tasks.end()
    , [task](TaskPtr t) { return task.get() == t.get(); }
  );

  if (it != Tasks.end())
  {
    Tasks.erase(it);
    return true;
  }

  return false;
}

bool Pool::Run2(
  uint64_t* pid
  , HEvent& h
  , TaskPtr task
  , TimePoint& t0
)
{
  if (pid)
    *pid = 0;

  WorkerPtr t;
  EventArray ev(StopEvent, FreeEvent);

  DoCompact();

  for (int loop = 0; !Stopping; ++loop)
  {
    size_t allSize{};
    t = PopUnused(allSize);

    if (t == nullptr)
    {
      if (allSize >= MaxThreads)
      {
        if (Mode == OVERFLOW_MODE::FAIL)
        {
          Errors++;
          return true;
        }

        auto rc = WaitForMultipleObjects(ev, false);
        if (rc == WAIT_RESULT::OBJECT_0)
        {
          auto e = t0.ElapsedSince();

          if (e > 200)
          {
            LogW("loops=%i, spent=%lli, total=%lli, threads=%lli", loop, e, (int64_t)LockedInRun, allSize);
          }

          return true;
        }

        continue;
      }

      if (DequeueTask(task) == false)
        return false;

      task->Worker = CreateWorker(t0, task->Callback, pid, task->ThreadHandle);
      CreateInvoke++;
      return true;
    }

    break;
  }

  if (DequeueTask(task) == false)
  {
    Push(Unused, t);
    return false;
  }

  uint64_t id{};
  task->ThreadHandle = t->Invoke(task->Callback, id);
  if (task->ThreadHandle)
  {
    if (pid != nullptr)
      *pid = id;

    SlowInvoke++;
    return true;
  }

  Push(Unused, t);
  Errors++;
  return true;
}

HEvent Pool::Run(TCallback cb, uint64_t* pid)
{
  TimePoint t0;
  TaskPtr task = QueueTask(cb);

  if (pid)
    *pid = 0;

  bool dequeued = Run2(pid, task->ThreadHandle, task, t0);

  TimePoint t1;
  LockedInRunInvoke += t1.ElapsedSince();
  LockedInRun += t0.ElapsedSince();

  if (dequeued == false)
  {
    if (pid != nullptr)
      *pid = task->Worker->GetTid();

    DirectInvoke++;
    return task->ThreadHandle;
  }

  return task->ThreadHandle;
}

void Pool::Locked_Find(Worker* p, bool& all, bool& unused)
{
  WorkerPtr t = p->Get();

  all = false;
  for (auto& e : All)
  {
    if (e.get() != p)
      continue;
    
    all = true;
    break;
  }

  unused = false;
  for (auto& e : Unused)
  {
    if (e.get() != p)
      continue;

    unused = true;
    break;
  }
}

void Pool::CB_OnTimer(Worker* p)
{
  OnTimerCalls++;

  if (Lock.try_lock())
  {
    Owner = GetCurrentThreadId();

    Locked_StopExpired(p);
    Lock.unlock();
  }
}

static void YieldThread()
{
#ifdef _WIN32
  ::SwitchToThread();
#else
  ::sched_yield();
#endif
}

TaskPtr Pool::CB_OnFree(Worker* p)
{
  if (TaskLock.try_lock())
  {
    if (Tasks.empty() == false)
    {
      TaskPtr task = Tasks.front();
      Tasks.pop_front();

      task->ThreadHandle = p->Handle();
      task->Worker = p->shared_from_this();
      ResetEvent(task->ThreadHandle);

      DirectInvoke++;
      TaskLock.unlock();
      return task;
    }

    TaskLock.unlock();
  }

  LOCK_GUARD();

#ifdef _DEBUG  
  bool all{}, unused{};
  Locked_Find(p, all, unused);
  assert(all == true && unused == false);
#endif

  if (Unused.size() + 1 > MaxUnusedThreads)
  {
    p->SetExpireTimer(MaxIdleTime);
    SET_TIMER();
  }

  WorkerPtr t = p->Get();
  Unused.push_front(t);
  SetEvent(FreeEvent);

  return TaskPtr();
}

void Pool::Locked_StopExpired(Worker* caller)
{
  CancelWaitableTimer(Timer);

  if (Stopping)
    return;

  bool setTimer = false;
  for (bool cont = true; cont;)
  {
    cont = false;

    for (auto it = Unused.begin(); it != Unused.end(); ++it)
    {
      WorkerPtr e = *it;
      if (!e->IsExpired())
        continue;

      if (caller && e.get() == caller)
      {
        setTimer = true;
        continue;
      }

      auto ita = std::find_if(
        All.begin()
        , All.end()
        , [e](WorkerPtr t) { return e.get() == t.get(); }
      );
      
      assert(ita != All.end());

      e->Stop();
      ThreadsStopped++;

      auto c0 = e.use_count();
      assert(c0 == 3);

      Unused.erase(it);
      ThreadsUnused = Unused.size();

      All.erase(ita);
      ThreadsTotal = All.size();

      auto c1 = e.use_count();
      assert(c1 == 1);

      cont = true;
      break;
    }
  }

  if (setTimer)
    SET_TIMER();
}
