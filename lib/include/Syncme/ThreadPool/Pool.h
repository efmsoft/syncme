#pragma once

#include <list>
#include <mutex>
#include <stdint.h>

#include <Syncme/ThreadPool/Worker.h>

namespace Syncme
{
  namespace ThreadPool
  {
    typedef std::list<WorkerPtr> WorkerList;

    class Pool
    {
      size_t MaxSize;

      std::mutex Lock;
      uint64_t Owner;
      bool Stopping;

      WorkerList All;
      WorkerList Free;
      WorkerList Deleting;

    public:
      Pool();
      ~Pool();

      HEvent Run(TCallback cb, uint64_t* pid = nullptr);
      void Stop();

    private:
      bool OnFree(Worker* p);
      bool OnExit(Worker* p);
      void SetStopping();

      WorkerPtr PopFree();
      void Push(WorkerList& list, WorkerPtr t);

      void CompleteDelete();
    };
  }
}