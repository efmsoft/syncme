#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

#include <Syncme/Sync.h>
#include <Syncme/Timer/Timer.h>

namespace Syncme
{
  namespace Implementation
  {
    struct TimerQueue;
    typedef std::shared_ptr<TimerQueue> TimerQueuePtr;

    struct TimerQueue
    {
      static std::recursive_mutex Lock;

      HEvent EvStop;
      HEvent EvUpdate;

      std::shared_ptr<std::jthread> Thread;
      TimerList Queue;

    public:
      SINCMELNK TimerQueue();
      SINCMELNK ~TimerQueue();

      SINCMELNK bool SetTimer(
        HEvent timer
        , long dueTime
        , long period
        , std::function<void(HEvent)> callback
      );
      SINCMELNK bool CancelTimer(Syncme::Event* timer);
      bool Empty() const;

      static TimerQueuePtr& Ptr();

    private:
      // Protected by Lock. Zero means no wait needs interrupting (the worker
      // is active, or an update is already pending); UINT64_MAX means FOREVER.
      uint64_t WakeDeadline;
      std::chrono::steady_clock::time_point WakeSteadyDeadline;

      void WakeForEarlierTimer(uint64_t dueTime, long delay);
      void Stop();
      void Worker();

      bool TryLock();
      bool GetSleepTime(uint32_t& ms);
      void SignallTimers();
      bool SignallOne();
    };
  }
}