#include <cassert>

#include <Syncme/ProcessThreadId.h>
#include <Syncme/ThreadPool/Pool.h>

using namespace Syncme::ThreadPool;

static const size_t MAX_POOL_SIZE = 12;
static const int64_t EXIT_AFTER = 3; // 3 sec

Pool::Pool()
  : MaxSize(MAX_POOL_SIZE)
  , Owner(0)
  , Stopping(false)
{
}

Pool::~Pool()
{
}

void Pool::SetStopping()
{
  std::lock_guard<std::mutex> guard(Lock);
  Owner = GetCurrentThreadId();
  Stopping = true;
}

void Pool::Stop()
{
  SetStopping();

  for (auto& e : All)
    e->Stop();

  std::lock_guard<std::mutex> guard(Lock);
  Owner = GetCurrentThreadId();

  assert(All.size() == Free.size());

  Free.clear();
  All.clear();

  CompleteDelete();
}

void Pool::CompleteDelete()
{
  Deleting.clear();
}

WorkerPtr Pool::PopFree()
{
  std::lock_guard<std::mutex> guard(Lock);

  Owner = GetCurrentThreadId();
  CompleteDelete();

  if (Stopping || Free.empty())
    return WorkerPtr();

  auto t = Free.front();
  Free.pop_front();
  
  return t;
}

void Pool::Push(WorkerList& list, WorkerPtr t)
{
  std::lock_guard<std::mutex> guard(Lock);
  Owner = GetCurrentThreadId();

  list.push_back(t);
}

HEvent Pool::Run(TCallback cb, uint64_t* pid)
{
  if (pid)
    *pid = 0;

  auto t = PopFree();
  if (t == nullptr)
  {
    TOnIdle notifyIdle = std::bind(&Pool::OnFree, this, std::placeholders::_1);
    TOnExit notifyExit = std::bind(&Pool::OnExit, this, std::placeholders::_1);
    t = std::make_shared<Worker>(notifyIdle, notifyExit);

    if (!t->Start())
      return nullptr;

    Push(All, t);
  }

  uint64_t id;
  HEvent h = t->Invoke(cb, id);
  if (h)
  {
    if (pid != nullptr)
      *pid = id;

    return h;
  }
    
  Push(Free, t);
  return nullptr;
}

bool Pool::OnFree(Worker* p)
{
  WorkerPtr t;

  std::lock_guard<std::mutex> guard(Lock);
  Owner = GetCurrentThreadId();

  t = p->Get();

  bool all = false;
  for (auto& e : All)
  {
    if (e.get() == p)
    {
      assert(t.get() == p);

      all = true;
      break;
    }
  }

  bool free = false;
  for (auto& e : Free)
  {
    if (e.get() == p)
    {
      free = true;
      break;
    }
  }

  assert(all == true && free == false);

  if (Free.size() >= MaxSize)
    p->SetExitTimer(EXIT_AFTER);

  Free.push_back(t);
  return true;
}

bool Pool::OnExit(Worker* p)
{
  WorkerPtr t;

  std::lock_guard<std::mutex> guard(Lock);

  Owner = GetCurrentThreadId();
  CompleteDelete();

  t = p->Get();

  bool all = false;
  for (auto& e : All)
  {
    if (e.get() == p)
    {
      assert(t.get() == p);

      all = true;
      break;
    }
  }

  bool free = false;
  for (auto& e : Free)
  {
    if (e.get() == p)
    {
      free = true;
      break;
    }
  }

  if (!all || !free || Stopping)
    return false;

  for (auto it = Free.begin(); it != Free.end(); ++it)
  {
    if (it->get() == p)
    {
      Free.erase(it);
      break;
    }
  }

  for (auto it = All.begin(); it != All.end(); ++it)
  {
    if (it->get() == p)
    {
      // We can not delete Worker item in the context of call from Worker::EntryPoint()
      // We postpone it and delete on a next ThreadPool call
      Deleting.push_back(t);
      All.erase(it);
      
      // Release before releasing lock!!!
      t.reset();
      return true;
    }
  }

  assert("!?!? how can it be");
  return false;
}