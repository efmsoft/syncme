#pragma once

#include <memory>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include <Syncme/Sync.h>
#include <Syncme/Sockets/SocketEvent.h>

namespace Syncme
{
  namespace Implementation
  {
    struct SocketEventQueue;
    typedef std::shared_ptr<SocketEventQueue> SocketEventQueuePtr;

    struct SocketEventQueue
    {
      static CS RemoveLock;

      CS DataLock;
      HEvent EvStop;
      HEvent EvGrowDone;

      std::shared_ptr<std::jthread> Thread;
      SocketEventMap Queue;

#ifndef _WIN32
      int Poll;
      int ControlSocket;
      int Port;
      std::vector<epoll_event> Events;
#endif      

    public:
      SocketEventQueue(int& rc, unsigned minPort = 10000, unsigned maxPort = UINT16_MAX);
      ~SocketEventQueue();

      bool AddSocketEvent(SocketEvent* socketEvent);
      bool RemoveSocketEvent(SocketEvent* socketEvent);
      bool Empty();

      bool ActivateEvent(SocketEvent* socketEvent);

    protected:
      friend struct SocketEvent;
      friend HEvent Syncme::CreateSocketEvent(int socket, int eventMask);
      static SocketEventQueuePtr& Ptr();

    private:
      void Stop();
      void Worker();

#ifndef _WIN32
      enum class OPTIONS
      {
        GROW_SIZE = 64
      };

      enum class ADD_EVENT_RESULT
      {
        FAILED,
        SUCCESS,
        GROW
      };

      ADD_EVENT_RESULT Append(SocketEvent* socketEvent);

      void FireEvents(const epoll_event& e);
      bool ProcessEvents(int n);

      int Bind(unsigned minPort, unsigned maxPort);
      int SendToSelf(const void* data, uint32_t size);
      int SendTo(const sockaddr_in* addr, const void* data, uint32_t size);
#endif      
    };
  }
}