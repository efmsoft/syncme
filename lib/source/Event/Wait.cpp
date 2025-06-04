#include <cassert>
#include <map>
#include <mutex>
#include <vector>

#include <Syncme/Sync.h>
#include <Syncme/Event/Event.h>

using namespace Syncme;

namespace Syncme
{
  typedef std::map<Event*, uint32_t> CookieMap;

  class WaitContext
  {
    std::mutex Lock;
    CookieMap Cookies;

    bool WaitAll;
    HEvent Event;
    std::vector<bool> Bits;
    size_t FirstSignalled;
    bool Failed;

  public:
    WaitContext(bool waitAll, size_t count)
      : WaitAll(waitAll)
      , Event(CreateNotificationEvent())
      , Bits(count)
      , FirstSignalled(count)
      , Failed(false)
    {
    }

    CookieMap& GetCookieMap()
    {
      return Cookies;
    }

    bool Completed(size_t count)
    {
      if (WaitAll && count > Bits.size())
        return false;

      return GetEventState(Event) == STATE::SIGNALLED;
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
          , [cookie](const auto& it) { return it.second == cookie; }
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
        SetEvent(Event);
    }

    WAIT_RESULT Wait(uint32_t ms)
    {
      WAIT_RESULT rc = WAIT_RESULT::OBJECT_0;

      if (GetEventState(Event) != STATE::SIGNALLED)
        rc = WaitForSingleObject(Event, ms);

      CookieMap cookies;
      if (true)
      {
        std::lock_guard<std::mutex> guard(Lock);

        cookies = Cookies;
        Cookies.clear();
      }

      for (auto& c : cookies)
      {
        auto f = c.first->UnregisterWait(c.second);
        assert(f || c.first->Closing);
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
  auto& cookies = context.GetCookieMap();

  size_t index = 0;
  for (auto& e : events)
  {
    auto cookie = e->RegisterWait(std::bind_front(&WaitContext::EventSignalled, &context, index++));
    cookies[e.get()] = cookie;

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
  if (event->Closing)
    return WAIT_RESULT::FAILED;

  return f ? WAIT_RESULT::OBJECT_0 : WAIT_RESULT::TIMEOUT;
}