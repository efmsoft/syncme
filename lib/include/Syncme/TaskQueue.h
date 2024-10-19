#pragma once

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <thread>

#include <Syncme/Api.h>
#include <Syncme/CritSection.h>
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

    private:
      void WaitForCompletion() const;
      static void DefaultCallback(void* context);
    };

    typedef std::shared_ptr<Item> ItemPtr;
    typedef std::list<ItemPtr> ItemList;

    class Queue
    {
      CritSection Lock;
      ItemList Items;

      HEvent EventStop;
      HEvent EventWakeup;
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