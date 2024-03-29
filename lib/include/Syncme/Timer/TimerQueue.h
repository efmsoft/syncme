#pragma once

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
      void Stop();
      void Worker();

      bool TryLock();
      bool GetSleepTime(uint32_t& ms);
      void SignallTimers();
      bool SignallOne(TimerList& reset);
    };
  }
}