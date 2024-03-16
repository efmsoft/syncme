#pragma once

#include <functional>
#include <memory>
#include <stdint.h>
#include <thread>

#include <Syncme/Api.h>
#include <Syncme/CritSection.h>
#include <Syncme/Sync.h>

namespace Syncme
{
  namespace ThreadPool
  {
    class Worker;
    typedef std::function<void(void)> TCallback;
    typedef std::function<void(Worker*)> TOnIdle;
    typedef std::function<void(Worker*)> TOnTimer;

    typedef std::shared_ptr<Worker> WorkerPtr;

    class Worker : public std::enable_shared_from_this<Worker>
    {
      HEvent ManagementTimer;

      HEvent StopEvent;
      HEvent IdleEvent;
      HEvent BusyEvent;
      HEvent InvokeEvent;
      HEvent ExpireTimer;

      uint64_t ThreadID;
      std::shared_ptr<std::thread> Thread;
      
      bool Started;
      bool Stopped;
      bool Exited;

      TOnIdle NotifyIdle;
      TOnTimer OnTimer;
      TCallback Callback;

      CS StateLock;

    public:
      SINCMELNK Worker(
        HEvent managementTimer
        , TOnIdle notifyIdle
        , TOnTimer onTimer
      );
      SINCMELNK ~Worker();

      SINCMELNK bool Start();
      SINCMELNK void Stop();

      SINCMELNK HEvent Invoke(TCallback cb, uint64_t& id);
      SINCMELNK WorkerPtr Get();

      SINCMELNK void SetExpireTimer(long ms);
      SINCMELNK void CancelExpireTimer();
      SINCMELNK bool IsExpired() const;

    private:
      void EntryPoint();
      HEvent Handle();
    };
  }
}