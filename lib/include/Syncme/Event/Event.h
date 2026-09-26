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

  // Intrusive doubly-linked-list node for one RegisterWait() subscription.
  // A caller embeds this directly in its own long-lived object (e.g.
  // Syncme::Socket) instead of Event allocating anything for it -- so
  // RegisterWait()/UnregisterWait() are O(1) list splice/unlink operations
  // regardless of how many OTHER waiters are registered on the SAME Event,
  // unlike the linear-scan flat vector this replaced. That distinction only
  // matters for an Event shared by many waiters at once (e.g.
  // ProxyServer's single process-wide ExitEvent, registered on by every
  // live Socket) -- a private, per-object event (e.g. SocketPair's own
  // CloseEvent) has at most a couple of waiters either way (VTune, 2026-09;
  // cache/wait-registry design discussion, 2026-09).
  //
  // Prev/Next/Owner are managed entirely by Event under its own
  // EventState::Lock; a caller must never touch them. A default-constructed
  // node is inert (Owner == nullptr) and safe to UnregisterWait() on -- that
  // just returns false, same as unregistering an already-removed cookie did.
  struct EventWaitNode
  {
    EventWaitNode* Prev = nullptr;
    EventWaitNode* Next = nullptr;
    class Event* Owner = nullptr;
    TWaitComplete Complete;
    uint32_t Cookie = 0;
  };

  class Event
  {
    std::shared_ptr<EventState> State;
    bool Closing;

  public:
    SINCMELNK Event(bool notification_event = true, bool signalled = false);
    SINCMELNK virtual ~Event();

    SINCMELNK virtual uint32_t Signature() const;
    SINCMELNK virtual void OnCloseHandle();

    SINCMELNK virtual uint32_t RegisterWait(EventWaitNode& node, TWaitComplete complete);
    SINCMELNK virtual bool UnregisterWait(EventWaitNode& node);

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