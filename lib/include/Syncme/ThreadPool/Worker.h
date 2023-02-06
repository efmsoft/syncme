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
    typedef std::function<bool(Worker*)> TOnIdle;
    typedef std::function<bool(Worker*)> TOnExit;

    typedef std::shared_ptr<Worker> WorkerPtr;

    class Worker : public std::enable_shared_from_this<Worker>
    {
      HEvent StopEvent;
      HEvent IdleEvent;
      HEvent BusyEvent;
      HEvent InvokeEvent;
      HEvent ExitTimer;

      uint64_t ThreadID;
      std::shared_ptr<std::thread> Thread;

      TOnIdle NotifyIdle;
      TOnExit NotifyExit;
      TCallback Callback;

    public:
      Worker(TOnIdle notifyIdle, TOnExit notifyExit);
      ~Worker();

      bool Start();
      void Stop();

      HEvent Invoke(TCallback cb, uint64_t& id);
      WorkerPtr Get();

      void SetExitTimer(int64_t nsec);

    private:
      void CancelExitTimer();

      void EntryPoint();
      HEvent Handle();
    };
  }
}