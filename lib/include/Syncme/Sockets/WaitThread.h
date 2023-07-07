#pragma once

#include <list>
#include <memory>

#include <Syncme/CritSection.h>
#include <Syncme/Sync.h>

namespace Syncme
{
  namespace Implementation
  {
    struct SocketEvent;

    class WaitThread
    {
      CS DataLock;
      std::list<SocketEvent*> Events;

      unsigned long ID;
      void* Thread;
      void* EvExit;
      void* EvRestart;
      void* EvDone;

    public:
      WaitThread();
      ~WaitThread();

      bool Add(SocketEvent* e);
      bool Remove(SocketEvent* e);

      bool Run();
      void Stop();
      bool Empty();

      void Worker();

    private:
      void CreateWaitList(std::vector<void*>& object);
      void TriggerEvent(void* h);
      
      bool RemoveInternal(SocketEvent* object);
      void Restart();
    };

    typedef std::shared_ptr<WaitThread> WaitThreadPtr;
  }
}