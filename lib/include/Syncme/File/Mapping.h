#pragma once

#include <Syncme/File/View.h>

typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES, * PSECURITY_ATTRIBUTES, * LPSECURITY_ATTRIBUTES;

namespace Syncme
{
  namespace File
  {
  #ifdef UNICODE
    typedef const wchar_t* PCString;
  #else
    typedef const char* PCString;
  #endif

    class Mapping
    {
      uint64_t MaximalWindow;
      bool ReadOnly;
      bool NoCache;

      Handle File;

      CS Lock;
      Handle MapHandle;
      uint64_t MappingSize;

      View Window;

    public:
      SINCMELNK Mapping(
        bool fReadOnly = true
        , uint64_t uMaxWndSize = DefaultWindowSize
        , bool nocache = false
      );
      SINCMELNK virtual ~Mapping();

      SINCMELNK virtual HResult OpenSection(
        PCString name
        , uint64_t size = 0
      );
      SINCMELNK virtual HResult OpenFile(
        PCString filename
        , PCString name = nullptr
        , uint64_t size = 0
      );
      SINCMELNK virtual HResult OpenFile(
        Handle h
        , PCString name = nullptr
        , uint64_t size = 0
      );

      SINCMELNK virtual HResult CreateSection(
        PCString name
        , uint64_t size = 0
        , LPSECURITY_ATTRIBUTES = nullptr
      );
      SINCMELNK virtual HResult CreateFile(
        PCString filename
        , PCString name
        , uint64_t size
        , uint32_t flags
      );

      SINCMELNK virtual HResult Resize(
        uint64_t size
        , PCString name = nullptr
        , LPSECURITY_ATTRIBUTES attributes = nullptr
      );
      SINCMELNK virtual void Close();

      SINCMELNK virtual void* Map(uint64_t offset, uint64_t size);
      SINCMELNK virtual void Unmap(void* address);

      SINCMELNK uint64_t GetMappingSize() const;
      SINCMELNK uint64_t GetMaxWindowSize() const;
      SINCMELNK long GetReferenceCount() const;

      SINCMELNK void SetMaxWindowSize(uint64_t mws);
    };
  }
}