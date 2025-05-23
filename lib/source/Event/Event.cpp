#include <algorithm>
#include <cassert>
#include <chrono>

#include <Syncme/Event/Counter.h>
#include <Syncme/Event/Event.h>

using namespace Syncme;

#define SIGNATURE *(uint32_t*)"Evnt";

std::atomic<uint64_t> Syncme::EventObjects{};
uint64_t Syncme::GetEventObjects() {return Syncme::EventObjects;}
std::atomic<uint32_t> Event::NextCookie{ 1 };
CS Event::RemoveLock;

Event::Event(bool notification_event, bool signalled)
  : Notification(notification_event)
  , Closing(false)
  , Signalled(signalled)
{
  EventObjects++;
}

Event::~Event()
{
  if (true)
  {
    std::lock_guard<std::mutex> guard(Lock);
    assert(Waits.empty());

    for (;;)
    {
      Event* c = PopRef();
      if (c == nullptr)
        break;

      c->RemoveRef(this);
    }
  }
   
  EventObjects--;
}

void EventDeleter::operator()(Event* p) const
{
  bool norefs = false;

  if (true)
  {
    std::lock_guard<std::mutex> guard(p->Lock);
    if (p->Closing == false)
      p->Closing = true;

    if (p->CrossRef.empty())
      norefs = true;
  }

  if (norefs)
  {
    delete p;
    return;
  }

  auto guard = Event::RemoveLock.Lock();
  delete p;
}

void Event::BindTo(Event* aliase)
{
  auto guard = RemoveLock.Lock();

  AddRef(aliase);
  aliase->AddRef(this);
}

uint32_t Event::Signature() const
{
  return SIGNATURE;
}

void Event::OnCloseHandle()
{
  std::lock_guard<std::mutex> guard(Lock);

  Closing = true;
  Signalled = true;

  Condition.notify_all();

  for (auto& w : Waits)
    w.second(w.first, true);

  Waits.clear();
}

void Event::SetEvent(Event* source)
{
  std::lock_guard<std::mutex> guard(Lock);
  
  Signalled = true;

  for (auto& w : Waits)
  {
    w.second(w.first, Closing);

    if (!Notification)
      Signalled = false;
  }

  if (Signalled)
  {
    auto dlock = DataLock.Lock();

    for (auto& c : CrossRef)
    {
      if (c != source)
        c->SetEvent(source);
    }
  }

  if (Notification)
    Condition.notify_all();
  else
    Condition.notify_one();
}

void Event::ResetEvent(Event* source)
{
  std::lock_guard<std::mutex> guard(Lock);

  Signalled = false;

  for (auto& c : CrossRef)
  {
    if (c != source)
      c->ResetEvent(source);
  }
}

bool Event::IsSignalled() const
{
  return Signalled;
}

bool Event::Wait(uint32_t ms)
{
  using namespace std::chrono_literals;

  std::unique_lock<std::mutex> guard(Lock);
  
  bool f = true;
  if (Signalled == false)
  {
    if (ms == FOREVER)
      Condition.wait(guard, [this] {return Signalled == true; });
    else
    {
      auto timeout = ms * 1ms;
      f = Condition.wait_for(guard, timeout, [this] {return Signalled == true; });
    }
  }

  if (f && !Notification)
    Signalled = false;

  return f;
}

void Event::AddRef(Event* dup)
{
  assert(dup);

  auto guard = DataLock.Lock();
  
  if (Closing == false)
    CrossRef.push_back(dup);
}

void Event::RemoveRef(Event* dup)
{
  auto guard = DataLock.Lock();
  
  auto it = std::find(CrossRef.begin(), CrossRef.end(), dup);

  if (it != CrossRef.end())
    CrossRef.erase(it);
}

Event* Event::PopRef()
{
  auto guard = DataLock.Lock();

  if (CrossRef.empty())
    return nullptr;

  auto t = CrossRef.front();
  CrossRef.pop_front();

  return t;
}

uint32_t Event::RegisterWait(TWaitComplete complete)
{
  uint32_t cookie = NextCookie++;

  std::lock_guard<std::mutex> guard(Lock);
  Waits[cookie] = complete;

  if (Signalled)
  {
    if (!Notification)
      Signalled = false;

    complete(cookie, Closing);
  }
  else if (Closing)
    complete(cookie, Closing);

  return cookie;
}

bool Event::UnregisterWait(uint32_t cookie)
{
  std::lock_guard<std::mutex> guard(Lock);

  auto it = Waits.find(cookie);
  if (it == Waits.end())
    return false;

  Waits.erase(cookie);
  return true;
}
