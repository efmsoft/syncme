#pragma once

#include <functional>
#include <memory>
#include <stdint.h>

#include <Syncme/Api.h>
#include <Syncme/Sync.h>

namespace Syncme
{
  enum class WAIT_RESULT;
  struct EventState;

  typedef std::function<void(uint32_t cookie, bool failed)> TWaitComplete;

  class Event
  {
    std::shared_ptr<EventState> State;
    bool Closing;

  public:
    SINCMELNK Event(bool notification_event = true, bool signalled = false);
    SINCMELNK virtual ~Event();

    SINCMELNK virtual uint32_t Signature() const;
    SINCMELNK virtual void OnCloseHandle();

    SINCMELNK virtual uint32_t RegisterWait(TWaitComplete complete);
    SINCMELNK virtual bool UnregisterWait(uint32_t cookie);

  protected:

    void SetEvent(Event* source = nullptr);
    void ResetEvent(Event* source = nullptr);

    bool IsSignalled() const;
    virtual bool Wait(uint32_t ms);

    bool GetClosing() const;

  protected:
    friend struct EventDeleter;
    friend class WaitContext;
    friend WAIT_RESULT Syncme::WaitForSingleObject(HEvent event, uint32_t ms);
    friend WAIT_RESULT Syncme::WaitForMultipleObjects(const EventArray&, bool, uint32_t);
    friend HEvent Syncme::DuplicateHandle(HEvent event);
    friend bool Syncme::SetEvent(HEvent event);
    friend bool Syncme::ResetEvent(HEvent event);
    friend STATE Syncme::GetEventState(HEvent event);
    friend bool Syncme::GetEventClosed(HEvent event);

  private:
    Event(std::shared_ptr<EventState> state);
    Event* Duplicate() const;

    Event(const Event&) = delete;
    Event(Event&& src) noexcept = delete;
    Event& operator=(const Event&) = delete;
  };

  struct EventDeleter
  {
    void operator()(Event* p) const;
  };
}