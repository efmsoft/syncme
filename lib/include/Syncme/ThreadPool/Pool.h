#pragma once

#include <functional>
#include <list>
#include <mutex>
#include <stdint.h>

#include <Syncme/Api.h>
#include <Syncme/TimePoint.h>
#include <Syncme/ThreadPool/Worker.h>

namespace Syncme
{
  namespace ThreadPool
  {
    typedef std::list<WorkerPtr> WorkerList;
    typedef std::function<void(size_t)> SCompact;

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
      SCompact Compact;

      HEvent Timer;
      HEvent FreeEvent;
      HEvent StopEvent;

      std::mutex Lock;
      uint64_t Owner;
      bool Stopping;

      WorkerList All;
      WorkerList Unused;

      std::mutex TaskLock;
      TaskList Tasks;

    public:
      SINCMELNK Pool();
      SINCMELNK ~Pool();

      SINCMELNK void Stop();

      SINCMELNK HEvent Run(TCallback cb, uint64_t* pid = nullptr);

      SINCMELNK void StopUnused();

      SINCMELNK size_t GetMaxThread() const;
      SINCMELNK void SetMaxThreads(size_t size);

      SINCMELNK size_t GetMaxUnusedThreads() const;
      SINCMELNK void SetMaxUnusedThreads(size_t size);

      SINCMELNK long GetMaxIdleTime() const;
      SINCMELNK void SetMaxIdleTime(long t);

      SINCMELNK OVERFLOW_MODE GetOverflowMode() const;
      SINCMELNK void SetOverflowMode(OVERFLOW_MODE mode);

      SINCMELNK void SetCompact(SCompact compact);

    private:
      TaskPtr CB_OnFree(Worker* p);
      void CB_OnTimer(Worker* p);

      void DoCompact();

      void SetStopping();
      WorkerPtr PopUnused(size_t& allCount);
      void Push(WorkerList& list, WorkerPtr t);

      void Locked_StopExpired(Worker* caller);
      void Locked_Find(Worker* p, bool& all, bool& unused);
      
      WorkerPtr CreateWorker(
        const TimePoint& t0
        , TCallback cb
        , uint64_t* pid
        , HEvent& thread
      );

      TaskPtr QueueTask(TCallback cb);
      bool DequeueTask(TaskPtr task);

      bool Run2(uint64_t* pid, HEvent& h, TaskPtr task, TimePoint& t0);
    };
  }
}