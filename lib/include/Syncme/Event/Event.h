#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <vector>

#include <Syncme/CritSection.h>
#include <Syncme/Sync.h>

namespace Syncme
{
  enum class WAIT_RESULT;

  typedef std::function<void(uint32_t cookie, bool failed)> TWaitComplete;

  class Event
  {
    std::mutex Lock;
    bool Signalled;
    std::condition_variable Condition;

    CS DataLock;
    bool Notification;
    bool Closing;

    static std::atomic<uint32_t> NextCookie;
    std::map<uint32_t, TWaitComplete> Waits;

    static CS RemoveLock;
    std::list<Event*> CrossRef;

  public:
    Event(bool notification_event = true, bool signalled = false);
    virtual ~Event();

    virtual uint32_t Signature() const;
    virtual void OnCloseHandle();

  protected:

    void SetEvent(Event* source = nullptr);
    void ResetEvent(Event* source = nullptr);

    bool IsSignalled() const;
    virtual bool Wait(uint32_t ms);

  protected:
    friend class WaitContext;
    friend WAIT_RESULT Syncme::WaitForSingleObject(HEvent event, uint32_t ms);
    friend WAIT_RESULT Syncme::WaitForMultipleObjects(const EventArray&, bool, uint32_t);
    friend HEvent Syncme::DuplicateHandle(HEvent event);
    friend bool Syncme::SetEvent(HEvent event);
    friend bool Syncme::ResetEvent(HEvent event);
    friend STATE Syncme::GetEventState(HEvent event);

    virtual uint32_t RegisterWait(TWaitComplete complete);
    virtual bool UnregisterWait(uint32_t cookie);

    void AddRef(Event* dup);
    void RemoveRef(Event* dup);
    Event* PopRef();
    void BindTo(Event* aliase);

  private:
    Event(const Event&) = delete;
    Event(Event&& src) noexcept = delete;
    Event& operator=(const Event&) = delete;
  };
}