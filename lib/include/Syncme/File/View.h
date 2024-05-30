#pragma once

#include <mutex>
#include <stdint.h>

#include <Syncme/Api.h>
#include <Syncme/CritSection.h>

namespace Syncme
{
  namespace File
  {
    typedef void* Handle;
    typedef uint32_t HResult;
    typedef uint64_t HIFITEM;

    constexpr static uint64_t DefaultWindowSize = 1024 * 1024;

    #define LODW(l) ((DWORD_PTR)((uint64_t)(l) & 0xffffffff))
    #define HIDW(l) ((DWORD_PTR)((uint64_t)(l) >> 32))

    class View
    {
      uint64_t MaximalSize;
      bool ReadOnly;

      Handle MapHandle;
      uint64_t Size;

      CS Lock;
      void* Image;
      uint64_t Offset;
      uint64_t ViewSize;
      long Refs;

      static uint32_t Granularity;

    public:
      SINCMELNK View();
      SINCMELNK virtual ~View();

      SINCMELNK HResult Open(
        Handle map
        , uint64_t size
        , bool readonly = true
        , uint64_t maxwindow = DefaultWindowSize
      );
      SINCMELNK void Close();

      SINCMELNK void* Map(uint64_t ofgfset, uint64_t size);
      SINCMELNK void Unmap(void* address);

      SINCMELNK long GetReferenceCount() const;
      SINCMELNK HIFITEM Ptr2Offset(void* p) const;
    };
  }
}