#pragma once

#include <list>
#include <unordered_map>

#ifndef _WIN32
#include <sys/epoll.h>
#endif

#include <Syncme/Api.h>
#include <Syncme/Event/Event.h>
#include <Syncme/Sync.h>

#include <Syncme/Sockets/Queue.h>

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
#else
      bool Closed;
#endif

      Sockets::IO::Queue* TxQueue;

    public:
      SINCMELNK SocketEvent(int socket, int mask, Sockets::IO::Queue* txQueue = nullptr);
      SINCMELNK ~SocketEvent();

      SINCMELNK int GetEvents();
      SINCMELNK void FireEvents(int events);

      SINCMELNK void OnCloseHandle() override;
      SINCMELNK bool Wait(uint32_t ms) override;
      SINCMELNK uint32_t RegisterWait(TWaitComplete complete) override;
      SINCMELNK bool UnregisterWait(uint32_t cookie) override;
      SINCMELNK uint32_t Signature() const override;
      SINCMELNK static bool IsSocketEvent(HEvent h);

      SINCMELNK bool ExpectWrite() const;

#ifndef _WIN32
      SINCMELNK epoll_event GetPollEvent() const;
#else
      SINCMELNK void* GetWSAEvent() const;
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
