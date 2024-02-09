#pragma once

#include <list>
#include <unordered_map>

#ifndef _WIN32
#include <sys/epoll.h>
#endif

#include <Syncme/Event/Event.h>
#include <Syncme/Sync.h>

namespace Syncme
{
  namespace Implementation
  {
    struct SocketEvent : public Event
    {
      friend class WaitThread;

      int Socket;
      int EventMask;

      CS EventLock;
      int Events;

#ifdef _WIN32
      void* WSAEvent;
#endif

    public:
      SocketEvent(int socket, int mask);
      ~SocketEvent();

      int GetEvents();
      void FireEvents(int events);

      void OnCloseHandle() override;
      bool Wait(uint32_t ms) override;
      uint32_t RegisterWait(TWaitComplete complete) override;
      bool UnregisterWait(uint32_t cookie) override;
      uint32_t Signature() const override;
      static bool IsSocketEvent(HEvent h);

#ifndef _WIN32
      epoll_event GetPollEvent() const;
#endif
      
    private:
#ifdef _WIN32
      static void __stdcall WaitOrTimerCallback(
        void* lpParameter
        , unsigned char timerOrWaitFired
      );

      void Callback(bool timerOrWaitFired);
#endif
    };

    typedef std::list<SocketEvent*> SocketEventList;
    typedef std::unordered_map<SocketEvent*, bool> SocketEventMap;
  }
}
