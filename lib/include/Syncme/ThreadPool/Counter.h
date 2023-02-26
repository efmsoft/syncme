#pragma once

#include <atomic>
#include <stdint.h>

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
  }
}