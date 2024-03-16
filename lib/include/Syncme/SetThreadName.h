#pragma once

#include <stdint.h>

#include <Syncme/Api.h>

namespace Syncme
{
  SINCMELNK void SetThreadName(uint64_t threadID, const char* threadName);
  SINCMELNK void SetThreadName(uint64_t threadID, const wchar_t* threadName);
}

#ifdef _DEBUG
  #define SET_CUR_THREAD_NAME(n)  Syncme::SetThreadName(uint64_t(-1), n)
#else
  #define SET_CUR_THREAD_NAME(n)
#endif
