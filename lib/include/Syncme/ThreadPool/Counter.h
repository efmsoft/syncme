#pragma once

#include <atomic>
#include <stdint.h>

#include <Syncme/Api.h>

namespace Syncme
{
  namespace ThreadPool
  {
    extern std::atomic<uint64_t> ThreadsTotal;
    extern std::atomic<uint64_t> ThreadsUnused;
    extern std::atomic<uint64_t> ThreadsStopped;
    extern std::atomic<uint64_t> WorkersDescructed;
    extern std::atomic<uint64_t> LockedInRun;
    extern std::atomic<uint64_t> OnTimerCalls;
    extern std::atomic<uint64_t> Errors;

    SINCMELNK uint64_t GetThreadsTotal();
    SINCMELNK uint64_t GetThreadsUnused();
    SINCMELNK uint64_t GetThreadsStopped();
    SINCMELNK uint64_t GetWorkersDescructed();
    SINCMELNK uint64_t GetLockedInRun();
    SINCMELNK uint64_t GetOnTimerCalls();
    SINCMELNK uint64_t GetErrors();

    SINCMELNK uint64_t GetLockedInRunCreateWorker();
    SINCMELNK uint64_t GetLockedInRunStop();
    SINCMELNK uint64_t GetLockedInRunFail();
    SINCMELNK uint64_t GetLockedInRunInvoke();
    SINCMELNK uint64_t GetLockedInRunInvokeError();
    SINCMELNK uint64_t GetLockedInCompact();

  }
}