#pragma once

#include <atomic>
#include <stdint.h>

namespace Syncme
{
  extern std::atomic<uint64_t> EventObjects;
}