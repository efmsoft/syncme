#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include <Syncme/Event/Counter.h>
#include <Syncme/Event/Event.h>

using namespace Syncme;

#define SIGNATURE *(uint32_t*)"Evnt";

namespace Syncme
{
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

    // Intrusive doubly-linked list of currently-registered EventWaitNode's
    // (see Event.h) -- O(1) insert/remove regardless of length. Replaces a
    // flat vector with linear-scan erase that was fine for a private event
    // with 0-2 waiters but scaled badly for an event shared by many waiters
    // at once, e.g. ProxyServer's single process-wide ExitEvent, registered
    // on by every live Socket (wait-registry design discussion, 2026-09).
    EventWaitNode* WaitsHead = nullptr;
    EventWaitNode* WaitsTail = nullptr;
  };

  namespace
  {
    void LinkWait(EventState& state, EventWaitNode& node)
    {
      node.Prev = state.WaitsTail;
      node.Next = nullptr;

      if (state.WaitsTail)
        state.WaitsTail->Next = &node;
      else
        state.WaitsHead = &node;

      state.WaitsTail = &node;
    }

    void UnlinkWait(EventState& state, EventWaitNode& node)
    {
      if (node.Prev)
        node.Prev->Next = node.Next;
      else
        state.WaitsHead = node.Next;

      if (node.Next)
        node.Next->Prev = node.Prev;
      else
        state.WaitsTail = node.Prev;

      node.Prev = nullptr;
      node.Next = nullptr;
    }
  }
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

    for (auto* node = State->WaitsHead; node; node = node->Next)
      assert(node->Owner != this);
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

  EventWaitNode* node = State->WaitsHead;
  while (node)
  {
    EventWaitNode* next = node->Next;

    if (node->Owner == this)
    {
      auto cookie = node->Cookie;
      auto complete = node->Complete;

      UnlinkWait(*State, *node);
      node->Owner = nullptr;

      complete(cookie, true);
    }

    node = next;
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
    for (auto* node = State->WaitsHead; node; node = node->Next)
      node->Complete(node->Cookie, false);
  }
  else if (State->WaitsHead != nullptr)
  {
    State->Signalled = false;
    State->WaitsHead->Complete(State->WaitsHead->Cookie, false);
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

uint32_t Event::RegisterWait(EventWaitNode& node, TWaitComplete complete)
{
  // A node mid-registration (on this Event or a duplicate handle sharing
  // the same EventState) must be UnregisterWait()'d first -- re-linking it
  // here would silently corrupt whichever list it's already threaded into.
  assert(node.Owner == nullptr);

  uint32_t cookie = NextCookie++;

  std::lock_guard<std::mutex> guard(State->Lock);

  // Already closing: don't link at all. Leaving the node linked here (as
  // the old flat-vector Waits did, since RegisterWait() never erased on
  // this branch) meant the caller had to know a Complete(..., true) it
  // just received was really "still registered, still your job to
  // UnregisterWait()" -- easy to get wrong (WaitContext below relies on
  // exactly that distinction), and with an intrusive list, a caller who
  // gets it wrong leaves a live node pointing into memory it's about to
  // free. Reporting "closing" without ever linking removes the ambiguity:
  // a Complete(cookie, true) from RegisterWait() always means the node was
  // never linked and never needs UnregisterWait().
  if (Closing)
  {
    node.Cookie = cookie;
    complete(cookie, true);
    return cookie;
  }

  node.Owner = this;
  node.Complete = complete;
  node.Cookie = cookie;
  LinkWait(*State, node);

  if (State->Signalled)
  {
    if (State->Notification == false)
      State->Signalled = false;

    complete(cookie, false);
  }

  return cookie;
}

bool Event::UnregisterWait(EventWaitNode& node)
{
  std::lock_guard<std::mutex> guard(State->Lock);

  if (node.Owner != this)
    return false;

  UnlinkWait(*State, node);
  node.Owner = nullptr;

  return true;
}
