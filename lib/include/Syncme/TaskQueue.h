#pragma once

#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <Syncme/Api.h>
#include <Syncme/Sync.h>

namespace Syncme
{
  namespace Task
  {
    typedef void (*ItemCallback)(void* context);
    typedef std::function<void()> TCallback;

    struct Item
    {
      std::string Identifier;

      ItemCallback Callback;
      void* Context;

      TCallback Functor;
      HEvent Completed;
    
    public:
      SINCMELNK Item(ItemCallback p, void* context, const char* identifier);
      SINCMELNK Item(TCallback functor, const char* identifier);
      SINCMELNK ~Item();

      SINCMELNK void Invoke();
      SINCMELNK void Cancel();

      SINCMELNK void WaitForCompletion() const;
    private:
      static void DefaultCallback(void* context);
    };

    typedef std::shared_ptr<Item> ItemPtr;
    typedef std::list<ItemPtr> ItemList;

    class Queue
    {
      std::mutex Lock;
      std::condition_variable Wakeup;
      ItemList Items;
      bool StopRequested;
      std::thread Worker;

    public:
      SINCMELNK Queue();
      SINCMELNK ~Queue();

      SINCMELNK void Stop();

      SINCMELNK ItemPtr Schedule(ItemCallback p, void* context, const char* identifier = "");
      SINCMELNK ItemPtr Schedule(TCallback functor, const char* identifier);
      SINCMELNK ItemPtr Schedule(ItemPtr item);
      SINCMELNK bool Cancel(ItemPtr);

    private:
      void WorkerProc();
      ItemPtr PopItem();
    };
  }
}