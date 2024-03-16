#pragma once

#include <atomic>
#include <stdint.h>

#include <Syncme/Api.h>

namespace Syncme
{
  extern std::atomic<uint64_t> EventObjects;
  SINCMELNK uint64_t GetEventObjects();
}