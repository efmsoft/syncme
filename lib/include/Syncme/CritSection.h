#pragma once

#include <mutex>
#include <stdint.h>
#include <string>

#include <Syncme/Api.h>
#include <Syncme/Sync.h>

#define _CS_DETECT_LOCKS 0

#if defined(_WIN32) && defined(_DEBUG) && _CS_DETECT_LOCKS
#define CS_DETECT_LOCKS
#endif

namespace Syncme
{
  class CS
  {
#if CS_USE_CRITICAL_SECTION
    union
    {
      uint8_t SectionData[256];
  #ifdef _WINDOWS_
      CRITICAL_SECTION CriticalSection;
  #endif
    };
#else
    std::recursive_mutex Mutex;
    uint64_t OwningThread;
#endif

#ifdef CS_DETECT_LOCKS
    int MaxWait;
    void* MutexHandle;
    uint64_t OwningThread;
#endif

  public:
    SINCMELNK CS();
    SINCMELNK ~CS();

    class AutoLock
    {
      friend CS;
      CS* Section;

    public:
      SINCMELNK AutoLock(AutoLock&& src) noexcept;
      SINCMELNK ~AutoLock();

      SINCMELNK void Release();

    private:
      AutoLock() = delete;
      AutoLock(const AutoLock&) = delete;
      AutoLock(CS* section);

      AutoLock& operator=(const AutoLock&) = delete;
    };

    SINCMELNK const AutoLock Lock();

    SINCMELNK void Acquire();
    SINCMELNK void Release();

    SINCMELNK void SetMaxWait(int n);

  private:
    CS(const CS&) = delete;
    CS(CS&& src) noexcept = delete;
    CS& operator=(const CS&) = delete;
  };
}

using CritSection = Syncme::CS;
