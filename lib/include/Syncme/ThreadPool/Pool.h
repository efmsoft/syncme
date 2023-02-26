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

    enum class OVERFLOW_MODE
    {
      FAIL,
      WAIT
    };

    class Pool
    {
      size_t MaxUnusedThreads;
      size_t MaxThreads;
      long MaxIdleTime;
      OVERFLOW_MODE Mode;

      HEvent Timer;
      HEvent FreeEvent;
      HEvent StopEvent;

      std::mutex Lock;
      uint64_t Owner;
      bool Stopping;

      WorkerList All;
      WorkerList Unused;

    public:
      Pool();
      ~Pool();

      HEvent Run(TCallback cb, uint64_t* pid = nullptr);
      void Stop();

      void StopUnused();

      size_t GetMaxThread() const;
      void SetMaxThreads(size_t size);

      size_t GetMaxUnusedThreads() const;
      void SetMaxUnusedThreads(size_t size);

      long GetMaxIdleTime() const;
      void SetMaxIdleTime(long t);

      OVERFLOW_MODE GetOverflowMode() const;
      void SetOverflowMode(OVERFLOW_MODE mode);

    private:
      void CB_OnFree(Worker* p);
      void CB_OnTimer(Worker* p);

      void SetStopping();
      WorkerPtr PopUnused(size_t& allCount);
      void Push(WorkerList& list, WorkerPtr t);

      void Locked_StopExpired(Worker* caller);
      void Locked_Find(Worker* p, bool& all, bool& unused);
    };
  }
}