#include <Syncme/Sync.h>
#include <Syncme/Timer/Counter.h>
#include <Syncme/Timer/TimerQueue.h>
#include <Syncme/Timer/WaitableTimer.h>

using namespace Syncme::Implementation;

#define SIGNATURE *(uint32_t*)"WTmr";

std::atomic<uint64_t> Syncme::TimerObjects{};

WaitableTimer::WaitableTimer(bool notification_event)
  : Event(notification_event, false)
{
  TimerObjects++;
}

WaitableTimer::~WaitableTimer()
{
  TimerObjects--;
}

void WaitableTimer::OnCloseHandle()
{
  std::lock_guard<std::recursive_mutex> guard(TimerQueue::Lock);
  auto& queue = TimerQueue::Ptr();

  if (queue)
    queue->CancelTimer(this);

  Event::OnCloseHandle();
}

uint32_t WaitableTimer::Signature() const
{
  return SIGNATURE;
}

bool WaitableTimer::IsTimer(HEvent h)
{
  if (h == nullptr)
    return false;

  return h->Signature() == SIGNATURE;
}