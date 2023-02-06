#pragma once

#include <list>

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
      int Socket;
      int EventMask;

      CS EventLock;
      int Events;

#ifdef _WIN32
      void* UnregisterDone;
      void* WSAEvent;
      void* WaitObject;
#endif

    public:
      SocketEvent(int socket, int mask);
      ~SocketEvent();

      int GetEvents();
      void FireEvents(int events);

      void OnCloseHandle() override;
      bool Wait(uint32_t ms) override;
      uint32_t RegisterWait(TWaitComplete complete) override;
      uint32_t Signature() const override;
      static bool IsSocketEvent(HEvent h);

#ifndef _WIN32
      epoll_event GetPollEvent() const;
#endif
      
    private:
      void Update();

#ifdef _WIN32
      static void __stdcall WaitOrTimerCallback(
        void* lpParameter
        , unsigned char timerOrWaitFired
      );

      void Callback(bool timerOrWaitFired);
#endif
    };

    typedef std::list<SocketEvent*> SocketEventList;
  }
}
