#pragma once

#include <stdint.h>

#include <Syncme/Api.h>

namespace Syncme
{
  SINCMELNK uint64_t GetCurrentProcessId();
  SINCMELNK uint64_t GetCurrentThreadId();
  SINCMELNK uint64_t GetCurrentPThread();
}
 