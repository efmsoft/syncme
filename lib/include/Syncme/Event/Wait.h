#pragma once

#include <stdint.h>

namespace Syncme
{
  constexpr static uint32_t FOREVER = 0xffffffff;

  enum class WAIT_RESULT
  {
    OBJECT_0,
    OBJECT_1,
    OBJECT_2,
    OBJECT_3,
    OBJECT_4,
    OBJECT_5,
    OBJECT_6,
    OBJECT_7,

    TIMEOUT = 258,
    FAILED = -1
  };

  WAIT_RESULT WaitForSingleObject(HEvent event, uint32_t ms = FOREVER);
  WAIT_RESULT WaitForMultipleObjects(const EventArray& events, bool waitAll, uint32_t ms = FOREVER);
}