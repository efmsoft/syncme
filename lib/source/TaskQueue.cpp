#include <Syncme/SetThreadName.h>
#include <Syncme/TaskQueue.h>

using namespace Syncme;
using namespace Syncme::Task;

Queue::Queue()
  : EventStop(CreateNotificationEvent())
  , EventWakeup(CreateSynchronizationEvent())
  , Worker(&Queue::WorkerProc, this)
{
}

Queue::~Queue()
{
  Stop();
}

void Queue::Stop()
{
  SetEvent(EventStop);

  if (Worker.joinable())
    Worker.join();
}

ItemPtr Queue::Schedule(ItemCallback p, void* context, const char* identifier)
{
  ItemPtr item = std::make_shared<Item>(p, context, identifier);
  return Schedule(item);
}

ItemPtr Queue::Schedule(TCallback functor, const char* identifier)
{
  ItemPtr item = std::make_shared<Item>(functor, identifier);
  return Schedule(item);
}

ItemPtr Queue::Schedule(ItemPtr item)
{
  if (item)
  {
    auto guard = Lock.Lock();
    Items.push_back(item);

    SetEvent(EventWakeup);
  }
  return item;
}

bool Queue::Cancel(ItemPtr item)
{
  auto guard = Lock.Lock();

  for (auto it = Items.begin(); it != Items.end(); ++it)
  {
    if (*it == item)
    {
      item->Cancel();
      Items.erase(it);
      return true;
    }
  }

  return false;
}

ItemPtr Queue::PopItem()
{
  auto guard = Lock.Lock();

  if (Items.empty())
    return ItemPtr();

  ItemPtr item = Items.front();
  Items.pop_front();

  return item; 
}

void Queue::WorkerProc()
{
  SET_CUR_THREAD_NAME("Task::Queue::Worker");
  EventArray events(EventStop, EventWakeup);

  while (WaitForMultipleObjects(events, false) != WAIT_RESULT::OBJECT_0)
  {
    for (;;)
    {
      ItemPtr item = PopItem();
      if (item == nullptr)
        break;

      item->Invoke();
    }
  }
}
