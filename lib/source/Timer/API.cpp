#include <cassert>

#include <Syncme/Sync.h>
#include <Syncme/Timer/TimerQueue.h>
#include <Syncme/Timer/WaitableTimer.h>

using namespace Syncme::Implementation;

HEvent Syncme::CreateManualResetTimer()
{
  return std::shared_ptr<Event>(
    new WaitableTimer(true)
    , Syncme::EventDeleter()
  );
}

HEvent Syncme::CreateAutoResetTimer()
{
  return std::shared_ptr<Event>(
    new WaitableTimer(false)
    , Syncme::EventDeleter()
  );
}

bool Syncme::SetWaitableTimer(
  HEvent timer
  , long dueTime
  , long period
  , std::function<void(HEvent)> callback
)
{
  if (!WaitableTimer::IsTimer(timer))
  {
    assert(!"Not a timer");
    return false;
  }

  std::lock_guard<std::recursive_mutex> guard(TimerQueue::Lock);
  auto& queue = TimerQueue::Ptr();

  if (queue == nullptr)
    queue = std::make_shared<TimerQueue>();

  return queue->SetTimer(timer, dueTime, period, callback);
}

bool Syncme::CancelWaitableTimer(HEvent timer)
{
  if (!WaitableTimer::IsTimer(timer))
  {
    assert(!"Not a timer");
    return false;
  }

  std::lock_guard<std::recursive_mutex> guard(TimerQueue::Lock);
  auto& queue = TimerQueue::Ptr();

  if (queue == nullptr)
    return false;

  if (!queue->CancelTimer(timer.get()))
    return false;

  if (!queue->Empty())
    return true;

  queue.reset();
  return true;
}
