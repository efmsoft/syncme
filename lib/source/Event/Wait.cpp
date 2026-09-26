#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <Syncme/Sync.h>
#include <Syncme/Event/Event.h>

using namespace Syncme;

namespace Syncme
{
  class WaitContext
  {
    std::mutex Lock;

    // One node per event in the array, reserved up front and never resized
    // afterward: RegisterWait() links each node into its own Event's
    // intrusive wait list (see EventWaitNode, Event.h), so its address must
    // stay stable for as long as it's registered.
    std::vector<EventWaitNode> Nodes;

    // Parallel to Nodes: true while that index's registration still needs
    // an explicit UnregisterWait() in Wait()'s cleanup pass. Cleared early
    // by EventSignalled() when the Event already tore the node down itself
    // (OnCloseHandle, on failure) -- redundantly calling UnregisterWait()
    // on it again would be harmless (it just returns false) but pointless.
    std::vector<bool> Pending;

    bool WaitAll;

    // A plain flag + condvar instead of a real Event/EventState: nothing
    // ever calls RegisterWait() on this completion flag (only
    // EventSignalled()/Completed()/Wait() below touch it directly), so the
    // full Event machinery -- its own heap-allocated EventState, its own
    // wait registry -- bought nothing here. WaitForMultipleObjects()
    // constructs one WaitContext per call, so this was a
    // make_shared<EventState> on every call, not just every distinct set of
    // handles waited on (VTune, 2026-09).
    std::condition_variable Cond;
    bool Signalled;

    std::vector<bool> Bits;
    size_t FirstSignalled;
    bool Failed;

  public:
    WaitContext(bool waitAll, size_t count)
      : Nodes(count)
      , Pending(count, false)
      , WaitAll(waitAll)
      , Signalled(false)
      , Bits(count)
      , FirstSignalled(count)
      , Failed(false)
    {
    }

    EventWaitNode& NodeAt(size_t index)
    {
      return Nodes[index];
    }

    void MarkRegistered(size_t index)
    {
      std::lock_guard<std::mutex> guard(Lock);
      Pending[index] = true;
    }

    bool Completed(size_t count)
    {
      if (WaitAll && count > Bits.size())
        return false;

      std::lock_guard<std::mutex> guard(Lock);
      return Signalled;
    }

    void EventSignalled(size_t index, uint32_t cookie, bool failed)
    {
      std::lock_guard<std::mutex> guard(Lock);

      if (failed)
      {
        Failed = true;
        Pending[index] = false;
      }

      Bits[index] = true;
      if (FirstSignalled == Bits.size())
      {
        assert(index < Bits.size());
        FirstSignalled = index;
      }

      size_t n = 0;
      for (auto v : Bits)
        if (v)
          ++n;

      if (WaitAll == false || n == Bits.size() || failed)
      {
        Signalled = true;
        Cond.notify_all();
      }
    }

    WAIT_RESULT Wait(uint32_t ms, const EventArray& events)
    {
      WAIT_RESULT rc = WAIT_RESULT::OBJECT_0;

      {
        std::unique_lock<std::mutex> guard(Lock);
        if (!Signalled)
        {
          if (ms == FOREVER)
          {
            Cond.wait(guard, [this] { return Signalled; });
          }
          else
          {
            using namespace std::chrono_literals;
            bool signalled = Cond.wait_for(
              guard
              , ms * 1ms
              , [this] { return Signalled; }
            );

            if (!signalled)
              rc = WAIT_RESULT::TIMEOUT;
          }
        }
      }

      std::vector<size_t> pendingIndices;
      {
        std::lock_guard<std::mutex> guard(Lock);

        for (size_t i = 0; i < Pending.size(); ++i)
        {
          if (Pending[i])
            pendingIndices.push_back(i);
        }

        std::fill(Pending.begin(), Pending.end(), false);
      }

      for (size_t i : pendingIndices)
      {
        auto f = events[i]->UnregisterWait(Nodes[i]);
        assert(f || events[i]->GetClosing());
      }

      if (Failed)
        return WAIT_RESULT::FAILED;

      if (rc != WAIT_RESULT::OBJECT_0)
        return rc;

      return WAIT_RESULT(size_t(WAIT_RESULT::OBJECT_0) + FirstSignalled);
    }
  };
}

WAIT_RESULT Syncme::WaitForMultipleObjects(
  const EventArray& events
  , bool waitAll
  , uint32_t ms
)
{
  using namespace std::placeholders;
  
  WaitContext context(waitAll, events.size());

  size_t index = 0;
  for (auto& e : events)
  {
    e->RegisterWait(context.NodeAt(index), std::bind_front(&WaitContext::EventSignalled, &context, index));
    context.MarkRegistered(index);
    ++index;

    if (context.Completed(events.size()))
      break;
  }

  WAIT_RESULT rc = context.Wait(ms, events);
  return rc;
}

WAIT_RESULT Syncme::WaitForSingleObject(HEvent event, uint32_t ms)
{
  assert(event);

  if (event == nullptr)
    return WAIT_RESULT::FAILED;

  bool f = event->Wait(ms);
  if (event->GetClosing())
    return WAIT_RESULT::FAILED;

  return f ? WAIT_RESULT::OBJECT_0 : WAIT_RESULT::TIMEOUT;
}