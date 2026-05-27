#include <Syncme/SetThreadName.h>
#include <Syncme/TaskQueue.h>

using namespace Syncme;
using namespace Syncme::Task;

Queue::Queue()
  : StopRequested(false)
  , Worker(&Queue::WorkerProc, this)
{
}

Queue::~Queue()
{
  Stop();
}

void Queue::Stop()
{
  {
    std::lock_guard<std::mutex> guard(Lock);
    StopRequested = true;
  }

  Wakeup.notify_one();

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
    bool needWakeup = false;

    {
      std::lock_guard<std::mutex> guard(Lock);

      needWakeup = Items.empty();
      Items.push_back(item);
    }

    if (needWakeup)
      Wakeup.notify_one();
  }
  return item;
}

bool Queue::Cancel(ItemPtr item)
{
  std::lock_guard<std::mutex> guard(Lock);

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
  std::unique_lock<std::mutex> guard(Lock);

  Wakeup.wait(
    guard
    , [this]()
    {
      return StopRequested || !Items.empty();
    }
  );

  if (StopRequested || Items.empty())
    return ItemPtr();

  ItemPtr item = Items.front();
  Items.pop_front();

  return item;
}

void Queue::WorkerProc()
{
  SET_CUR_THREAD_NAME("Task::Queue::Worker");

  for (;;)
  {
    ItemPtr item = PopItem();
    if (item == nullptr)
      break;

    item->Invoke();
  }
}
