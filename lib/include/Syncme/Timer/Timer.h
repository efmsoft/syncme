#pragma once

#include <functional>
#include <list>
#include <memory>
#include <stdint.h>

#include <Syncme/Sync.h>

namespace Syncme
{
  namespace Implementation
  {
    struct Timer
    {
      HEvent EvTimer;
      long Period;
      std::function<void(HEvent)> Callback;

      uint64_t NextDueTime;

    public:
      SINCMELNK Timer(HEvent timer, long period, std::function<void(HEvent)> callback);
      SINCMELNK ~Timer();

      SINCMELNK void Set(long dueTime);
    };

    typedef std::shared_ptr<Timer> TimerPtr;
    typedef std::list<TimerPtr> TimerList;
  }
}
