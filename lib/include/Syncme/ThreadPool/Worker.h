#pragma once

#include <functional>
#include <memory>
#include <stdint.h>
#include <thread>

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
      bool Exited;

      TOnIdle NotifyIdle;
      TOnTimer OnTimer;
      TCallback Callback;

    public:
      Worker(
        HEvent managementTimer
        , TOnIdle notifyIdle
        , TOnTimer onTimer
      );
      ~Worker();

      bool Start();
      void Stop();

      HEvent Invoke(TCallback cb, uint64_t& id);
      WorkerPtr Get();

      void SetExpireTimer(long ms);
      void CancelExpireTimer();
      bool IsExpired() const;

    private:
      void EntryPoint();
      HEvent Handle();
    };
  }
}