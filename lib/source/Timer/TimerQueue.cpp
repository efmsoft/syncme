#include <cassert>

#include <Syncme/SetThreadName.h>
#include <Syncme/TickCount.h>
#include <Syncme/Timer/Counter.h>
#include <Syncme/Timer/TimerQueue.h>

#pragma warning(disable : 26110)

using namespace Syncme;
using namespace Syncme::Implementation;

static TimerQueuePtr Instance;
std::recursive_mutex TimerQueue::Lock;

std::atomic<uint64_t> Syncme::QueuedTimers{};
uint64_t Syncme::GetQueuedTimers() {return Syncme::QueuedTimers;}

TimerQueue::TimerQueue()
  : EvStop(CreateNotificationEvent())
  , EvUpdate(CreateSynchronizationEvent())
{
}

TimerQueue::~TimerQueue()
{
  Stop();
}

TimerQueuePtr& TimerQueue::Ptr()
{
  return Instance;
}

void TimerQueue::Stop()
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  if (Thread)
  {
    SetEvent(EvStop);
    Thread.reset();
  }

  Queue.clear();
  QueuedTimers = 0;
}

bool TimerQueue::SetTimer(
  HEvent timer
  , long dueTime
  , long period
  , std::function<void(HEvent)> callback
)
{
  assert(dueTime > 0);
  assert(period >= 0);
  assert(timer);

  if (dueTime <= 0 || period < 0 || timer == nullptr)
    return false;

  std::lock_guard<std::recursive_mutex> guard(Lock);

  for (auto it = Queue.begin(); it != Queue.end(); ++it)
  {
    auto& t = *it;

    if (t->EvTimer.get() == timer.get())
    {
      t->Set(dueTime);
      SetEvent(EvUpdate);
      return true;
    }
  }

  TimerPtr t = std::make_shared<Timer>(timer, period, callback);
  Queue.push_back(t);
  QueuedTimers++;

  t->Set(dueTime);

  if (Thread == nullptr)
    Thread = std::make_shared<std::jthread>(&TimerQueue::Worker, this);
  else
    SetEvent(EvUpdate);

  return true;
}

bool TimerQueue::CancelTimer(Syncme::Event* timer)
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  for (auto it = Queue.begin(); it != Queue.end(); ++it)
  {
    auto& t = *it;

    if (t->EvTimer.get() == timer)
    {
      Queue.erase(it);
      QueuedTimers--;
      return true;
    }
  }

  return false;
}

bool TimerQueue::Empty() const
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  return Queue.empty();
}

bool TimerQueue::TryLock()
{
  // Analogue of dwSpinCount for InitializeCriticalSectionEx()
  const uint32_t SPIN_COUNT = 100;

  for (uint32_t spin = 0;;)
  {
    if (Lock.try_lock())
      break;

    uint32_t ms = 0;
    if (spin++ < SPIN_COUNT)
    {
      ms = 5;
      spin = 0;
    }

    if (WaitForSingleObject(EvStop, ms) == WAIT_RESULT::OBJECT_0)
      return false;
  }

  return true;
}

bool TimerQueue::GetSleepTime(uint32_t& ms)
{
  if (!TryLock())
    return false;

  if (Queue.empty())
    ms = FOREVER;
  else
  {
    uint64_t min = (uint64_t)-1LL;

    for (auto& t : Queue)
    {
      if (t->NextDueTime < min)
        min = t->NextDueTime;
    }

    auto t = GetTimeInMillisec();

    if (t > min)
      ms = 0;
    else
      ms = uint32_t(min - t);
  }

  Lock.unlock();
  return true;
}

bool TimerQueue::SignallOne(TimerList& reset)
{
  auto now = GetTimeInMillisec();

  for (auto it = Queue.begin(); it != Queue.end(); ++it)
  {
    TimerPtr t = *it;
    if (t->NextDueTime <= now)
    {
      Queue.erase(it);
      QueuedTimers--;

      SetEvent(t->EvTimer);

      if (t->Callback)
        t->Callback(t->EvTimer);

      if (t->Period)
        reset.push_back(t);

      return true;
    }
  }

  return false;
}

void TimerQueue::SignallTimers()
{
  if (!TryLock())
    return;

  TimerList reset;

  for (;;)
    if (!SignallOne(reset))
      break;

  for (auto& t : reset)
  {
    t->Set(t->Period);

    Queue.push_back(t);
    QueuedTimers++;
  }
  
  if (!reset.empty())
    SetEvent(EvUpdate);

  Lock.unlock();
}

void TimerQueue::Worker()
{
  SET_CUR_THREAD_NAME("TimerQueue Worker");
  EventArray object(EvStop, EvUpdate);

  for (uint64_t dueTime{};;)
  {
    uint32_t ms{};
    if (!GetSleepTime(ms))
      break;

    auto rc = WaitForMultipleObjects(object, false, ms);
    if (rc == WAIT_RESULT::OBJECT_0)
      break;

    assert(rc == WAIT_RESULT::OBJECT_1 || rc == WAIT_RESULT::TIMEOUT);

    SignallTimers();
  }
}
