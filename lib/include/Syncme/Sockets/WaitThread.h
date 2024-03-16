#pragma once

#include <list>
#include <memory>

#include <Syncme/Api.h>
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
      SINCMELNK WaitThread();
      SINCMELNK ~WaitThread();

      SINCMELNK bool Add(SocketEvent* e);
      SINCMELNK bool Remove(SocketEvent* e);

      SINCMELNK bool Run();
      SINCMELNK void Stop();
      SINCMELNK bool Empty();

      SINCMELNK void Worker();

    private:
      void CreateWaitList(std::vector<void*>& object);
      void TriggerEvent(void* h);
      
      bool RemoveInternal(SocketEvent* object);
      void Restart();
    };

    typedef std::shared_ptr<WaitThread> WaitThreadPtr;
  }
}