#pragma once

#include <atomic>
#include <stdint.h>

#include <Syncme/Api.h>

namespace Syncme
{
  extern std::atomic<uint64_t> SocketEventObjects;
  SINCMELNK uint64_t GetSocketEventObjects();
}