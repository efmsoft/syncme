#include <cassert>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <utility>

#include <Syncme/Event/Counter.h>
#include <Syncme/Event/Event.h>

using namespace Syncme;

#define SIGNATURE *(uint32_t*)"Evnt";

namespace Syncme
{
  struct EventWait
  {
    Event* Owner;
    TWaitComplete Complete;
  };

  struct EventState
  {
    EventState(bool notification, bool signalled)
      : Notification(notification)
      , Signalled(signalled)
    {
    }

    std::mutex Lock;
    std::condition_variable Condition;

    bool Notification;
    bool Signalled;

    std::map<uint32_t, EventWait> Waits;
  };
}

std::atomic<uint64_t> Syncme::EventObjects{};
uint64_t Syncme::GetEventObjects() {return Syncme::EventObjects;}

static std::atomic<uint32_t> NextCookie{1};

Event::Event(bool notification_event, bool signalled)
  : State(std::make_shared<EventState>(notification_event, signalled))
  , Closing(false)
{
  EventObjects++;
}

Event::Event(std::shared_ptr<EventState> state)
  : State(std::move(state))
  , Closing(false)
{
  assert(State);
  EventObjects++;
}

Event::~Event()
{
  if (State)
  {
    std::lock_guard<std::mutex> guard(State->Lock);

    for (auto& wait : State->Waits)
      assert(wait.second.Owner != this);
  }

  EventObjects--;
}

void EventDeleter::operator()(Event* p) const
{
  delete p;
}

Event* Event::Duplicate() const
{
  std::lock_guard<std::mutex> guard(State->Lock);

  if (Closing)
    return nullptr;

  return new Event(State);
}

uint32_t Event::Signature() const
{
  return SIGNATURE;
}

void Event::OnCloseHandle()
{
  std::lock_guard<std::mutex> guard(State->Lock);

  if (Closing)
    return;

  Closing = true;
  State->Condition.notify_all();

  for (auto it = State->Waits.begin(); it != State->Waits.end();)
  {
    if (it->second.Owner != this)
    {
      ++it;
      continue;
    }

    auto cookie = it->first;
    auto complete = it->second.Complete;
    it = State->Waits.erase(it);

    complete(cookie, true);
  }
}

bool Event::GetClosing() const
{
  std::lock_guard<std::mutex> guard(State->Lock);
  return Closing;
}

void Event::SetEvent(Event* source)
{
  (void)source;

  std::lock_guard<std::mutex> guard(State->Lock);

  State->Signalled = true;

  if (State->Notification)
  {
    for (auto& wait : State->Waits)
      wait.second.Complete(wait.first, false);
  }
  else if (State->Waits.empty() == false)
  {
    auto it = State->Waits.begin();
    State->Signalled = false;
    it->second.Complete(it->first, false);
  }

  if (State->Notification)
    State->Condition.notify_all();
  else if (State->Signalled)
    State->Condition.notify_one();
}

void Event::ResetEvent(Event* source)
{
  (void)source;

  std::lock_guard<std::mutex> guard(State->Lock);
  State->Signalled = false;
}

bool Event::IsSignalled() const
{
  std::lock_guard<std::mutex> guard(State->Lock);
  return Closing || State->Signalled;
}

bool Event::Wait(uint32_t ms)
{
  using namespace std::chrono_literals;

  std::unique_lock<std::mutex> guard(State->Lock);

  bool f = true;
  if (State->Signalled == false && Closing == false)
  {
    if (ms == FOREVER)
    {
      State->Condition.wait(
        guard
        , [this]
          {
            return State->Signalled || Closing;
          }
      );
    }
    else
    {
      auto timeout = ms * 1ms;
      f = State->Condition.wait_for(
        guard
        , timeout
        , [this]
          {
            return State->Signalled || Closing;
          }
      );
    }
  }

  if (f && Closing == false && State->Signalled && State->Notification == false)
    State->Signalled = false;

  return f;
}

uint32_t Event::RegisterWait(TWaitComplete complete)
{
  uint32_t cookie = NextCookie++;

  std::lock_guard<std::mutex> guard(State->Lock);
  State->Waits[cookie] = EventWait{ this, complete };

  if (Closing)
  {
    complete(cookie, true);
  }
  else if (State->Signalled)
  {
    if (State->Notification == false)
      State->Signalled = false;

    complete(cookie, false);
  }

  return cookie;
}

bool Event::UnregisterWait(uint32_t cookie)
{
  std::lock_guard<std::mutex> guard(State->Lock);

  auto it = State->Waits.find(cookie);
  if (it == State->Waits.end() || it->second.Owner != this)
    return false;

  State->Waits.erase(it);
  return true;
}
