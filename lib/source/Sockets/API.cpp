#include <cassert>

#include <Syncme/Sync.h>
#include <Syncme/Sockets/SocketEvent.h>
#include <Syncme/Sockets/SocketEventQueue.h>

using namespace Syncme::Implementation;

HEvent Syncme::CreateSocketEvent(int socket, int eventMask)
{
  HEvent event = std::shared_ptr<Event>(
    new SocketEvent(socket, eventMask)
    , Syncme::EventDeleter()
  );

  if (event)
  {
#ifndef _WIN32    
    auto guard = SocketEventQueue::RemoveLock.Lock();
    auto& queue = SocketEventQueue::Ptr();

    if (queue == nullptr)
    {
      int e = 0;
      queue = std::make_shared<SocketEventQueue>(e);
      if (queue && e == -1)
      {
        queue.reset();
        return HEvent();
      }
    }

    if (!queue->AddSocketEvent((SocketEvent*)event.get()))
      return HEvent();
#endif      
  }
  return event;
}

int Syncme::GetSocketEvents(HEvent socketEvent)
{
  if (!SocketEvent::IsSocketEvent(socketEvent))
    return 0;

  SocketEvent* p = (SocketEvent*)socketEvent.get();
  return p->GetEvents();
}

#ifdef _WIN32
extern "C" SINCMELNK void __stdcall SetBeforeCloseSocketCallback(void* hook)
{
  // Do nothing. We use it only for communicating with IAgentX hook
}

extern "C" SINCMELNK void __stdcall SetBeforeShutdownSocketCallback(void* hook)
{
}

extern "C" SINCMELNK void __stdcall SetAfterRecvCallback(void* hook)
{
}

extern "C" SINCMELNK void __stdcall SetAfterRecvFromCallback(void* hook)
{
}

extern "C" SINCMELNK void __stdcall SetAfterSendCallback(void* hook)
{
}

extern "C" SINCMELNK void __stdcall SetAfterSendToCallback(void* hook)
{
}

extern "C" SINCMELNK void __stdcall SetAfterSocketCallback(void* hook)
{
}

extern "C" SINCMELNK void __stdcall SetAfterSetSockOptCallback(void* hook)
{
}

extern "C" SINCMELNK void __stdcall SetAfterIoctlSocketCallback(void* hook)
{
}

extern "C" SINCMELNK void __stdcall SetBeforeIoctlSocketCallback(void* hook)
{
}

extern "C" SINCMELNK void __stdcall SetAfterAcceptCallback(void* hook)
{
}

#endif
