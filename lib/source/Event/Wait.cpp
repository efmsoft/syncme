#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <vector>

#include <Syncme/Sync.h>
#include <Syncme/Event/Event.h>

using namespace Syncme;

namespace Syncme
{
  typedef std::pair<Event*, uint32_t> WaitCookie;
  typedef std::vector<WaitCookie> CookieList;

  class WaitContext
  {
    std::mutex Lock;
    CookieList Cookies;

    bool WaitAll;

    // A plain flag + condvar instead of a real Event/EventState: nothing
    // ever calls RegisterWait() on this completion flag (only
    // EventSignalled()/Completed()/Wait() below touch it directly), so the
    // full Event machinery -- its own heap-allocated EventState, its
    // std::map<uint32_t, EventWait> waiter registry -- bought nothing here.
    // WaitForMultipleObjects() constructs one WaitContext per call, so this
    // was a make_shared<EventState> on every call, not just every distinct
    // set of handles waited on (VTune, 2026-09).
    std::condition_variable Cond;
    bool Signalled;

    std::vector<bool> Bits;
    size_t FirstSignalled;
    bool Failed;

  public:
    WaitContext(bool waitAll, size_t count)
      : WaitAll(waitAll)
      , Signalled(false)
      , Bits(count)
      , FirstSignalled(count)
      , Failed(false)
    {
    }

    void AddCookie(Syncme::Event* event, uint32_t cookie)
    {
      std::lock_guard<std::mutex> guard(Lock);
      Cookies.emplace_back(event, cookie);
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

        auto it = std::find_if(
          Cookies.begin()
          , Cookies.end()
          , [cookie](const auto& wait)
            {
              return wait.second == cookie;
            }
        );

        if (it != Cookies.end())
          Cookies.erase(it);
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

    WAIT_RESULT Wait(uint32_t ms)
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

      CookieList cookies;
      if (true)
      {
        std::lock_guard<std::mutex> guard(Lock);

        cookies = Cookies;
        Cookies.clear();
      }

      for (auto& c : cookies)
      {
        auto f = c.first->UnregisterWait(c.second);
        assert(f || c.first->GetClosing());
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
    auto cookie = e->RegisterWait(std::bind_front(&WaitContext::EventSignalled, &context, index++));
    context.AddCookie(e.get(), cookie);

    if (context.Completed(events.size()))
      break;
  }

  WAIT_RESULT rc = context.Wait(ms);
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