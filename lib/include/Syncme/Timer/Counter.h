#pragma once

#include <atomic>
#include <stdint.h>

#include <Syncme/Api.h>

namespace Syncme
{
  extern std::atomic<uint64_t> TimerObjects;
  extern std::atomic<uint64_t> QueuedTimers;
  SINCMELNK uint64_t GetTimerObjects();
  SINCMELNK uint64_t GetQueuedTimers();
}