#include <cassert>

#include <Logme/Logme.h>

#ifdef _WIN32

#define NOMINMAX
#include <windows.h>

#include <Syncme/File/Mapping.h>

using namespace Syncme::File;

Mapping::Mapping(bool readonly, uint64_t maximal_size, bool nocache)
{
  MaximalWindow = maximal_size;
  ReadOnly = readonly;
  NoCache = nocache;

  File = INVALID_HANDLE_VALUE;

  MapHandle = nullptr;
  MappingSize = 0;
}

Mapping::~Mapping()
{
  Close();
}

HResult Mapping::OpenSection(PCString name, uint64_t size)
{
  HResult hr = E_FAIL;
  auto guard = Lock.Lock();

  Close();

  if (name && *name)
  {
    File = INVALID_HANDLE_VALUE;
    MappingSize = size ? size : MaximalWindow;

    MapHandle = ::OpenFileMapping(
      ReadOnly ? FILE_MAP_READ : FILE_MAP_WRITE
      , false
      , name
    );

    if (MapHandle != nullptr)
      hr = Window.Open(MapHandle, MappingSize, ReadOnly, MaximalWindow);
    else
    {
      int err = GetLastError();
      hr = HRESULT_FROM_WIN32(err);

      LogmeE("OpenFileMapping failed. Error: %s", OSERR(err));
    }
  }
  else
    hr = E_POINTER;

  if (FAILED(hr))
    Close();

  return hr;
}

HResult Mapping::CreateSection(
  PCString name
  , uint64_t size
  , LPSECURITY_ATTRIBUTES attributes
)
{
  HResult hr = E_FAIL;
  auto guard = Lock.Lock();

  Close();

  if (name && *name)
  {
    File = INVALID_HANDLE_VALUE;
    MappingSize = size ? size : MaximalWindow;

    MapHandle = ::CreateFileMapping(
      File
      , attributes
      , (ReadOnly ? PAGE_READONLY : PAGE_READWRITE) |
        (NoCache ? SEC_NOCACHE | SEC_COMMIT : 0)
      , HIDW(MappingSize)
      , LODW(MappingSize)
      , name
    );

    if (MapHandle != nullptr)
      hr = Window.Open(MapHandle, MappingSize, ReadOnly, MaximalWindow);
    else
    {
      int err = GetLastError();
      hr = HRESULT_FROM_WIN32(err);

      LogmeE("CreateFileMapping failed. Error: %s", OSERR(err));
    }
  }
  else
    hr = E_POINTER;

  if (FAILED(hr))
    Close();

  return hr;
}

HResult Mapping::OpenFile(PCString filename, PCString name, uint64_t size)
{
  HResult hr = E_FAIL;
  auto guard = Lock.Lock();

  Close();

  if (filename && *filename)
  {
    DWORD dwAttr = GetFileAttributes(filename) & (~FILE_ATTRIBUTE_READONLY);
    SetFileAttributes(filename, dwAttr & 0xFFFF);

    File = ::CreateFile(
      filename, ReadOnly ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE
      , FILE_SHARE_WRITE | FILE_SHARE_READ
      , nullptr
      , OPEN_EXISTING
      , FILE_ATTRIBUTE_ARCHIVE 
        | FILE_ATTRIBUTE_HIDDEN 
        | FILE_ATTRIBUTE_READONLY 
        | FILE_ATTRIBUTE_SYSTEM
      , nullptr
    );

    if (File != INVALID_HANDLE_VALUE)
    {
      if (!size)
        MappingSize = GetFileSize(File, nullptr);
      else
        MappingSize = size;

      if (MappingSize)
      {
        if (MappingSize != INVALID_FILE_SIZE)
        {
          MapHandle = ::CreateFileMapping(
            File
            , nullptr
            , ReadOnly ? PAGE_READONLY : PAGE_READWRITE
            , HIDW(MappingSize)
            , LODW(MappingSize)
            , name
          );

          if (MapHandle != nullptr)
            hr = Window.Open(MapHandle, MappingSize, ReadOnly, MaximalWindow);
          else
          {
            int err = GetLastError();
            hr = HRESULT_FROM_WIN32(err);

            LogmeE("Window.Open() failed. Error: %s", OSERR(err));
          }
        }
        else
        {
          int err = GetLastError();
          hr = HRESULT_FROM_WIN32(err);

          LogmeE("GetFileSize failed. Error: %s", OSERR(err));
        }
      }
      else
        hr = HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
    }
    else
    {
      int err = GetLastError();
      hr = HRESULT_FROM_WIN32(err);
    }
  }
  else
    hr = E_POINTER;

  if (FAILED(hr))
    Close();

  return hr;
}

HResult Mapping::OpenFile(HANDLE h, PCString name, uint64_t size)
{
  HResult hr = E_FAIL;
  auto guard = Lock.Lock();

  Close();

  if (h && h != INVALID_HANDLE_VALUE)
  {
    bool f = ::DuplicateHandle(
      GetCurrentProcess()
      , h
      , GetCurrentProcess()
      , &File
      , ReadOnly ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE
      , false
      , 0
    );

    if (f && File != INVALID_HANDLE_VALUE)
    {
      if (!size)
        MappingSize = GetFileSize(File, nullptr);
      else
        MappingSize = size;

      if (MappingSize)
      {
        if (MappingSize != INVALID_FILE_SIZE)
        {
          MapHandle = ::CreateFileMapping(
            File
            , nullptr
            , ReadOnly ? PAGE_READONLY : PAGE_READWRITE
            , HIDW(MappingSize)
            , LODW(MappingSize)
            , name
          );

          if (MapHandle != nullptr)
            hr = Window.Open(MapHandle, MappingSize, ReadOnly, MaximalWindow);
          else
          {
            int err = GetLastError();
            hr = HRESULT_FROM_WIN32(err);

            LogmeE("Window.Open() failed. Error: %s", OSERR(err));
          }
        }
        else
        {
          int err = GetLastError();
          hr = HRESULT_FROM_WIN32(err);

          LogmeE("GetFileSize failed. Error: %s", OSERR(err));
        }
      }
      else
        hr = HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
    }
    else
    {
      int err = GetLastError();
      hr = HRESULT_FROM_WIN32(err);

      LogmeE("DuplpicateHandle failed. Error: %s", OSERR(err));
    }
  }
  else
    hr = E_POINTER;

  if (FAILED(hr))
    Close();

  return hr;
}

HResult Mapping::CreateFile(
  PCString filename
  , PCString name
  , uint64_t size
  , uint32_t flags
)
{
  HResult hr = E_FAIL;
  auto guard = Lock.Lock();

  Close();

  if (filename && *filename)
  {
    DWORD dwAttr = GetFileAttributes(filename) & (~FILE_ATTRIBUTE_READONLY);
    SetFileAttributes(filename, dwAttr & 0xFFFF);

    File = ::CreateFile(
      filename
      , ReadOnly ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE
      , FILE_SHARE_WRITE | FILE_SHARE_READ
      , nullptr
      , CREATE_ALWAYS
      , flags
      , nullptr
    );

    if (File != INVALID_HANDLE_VALUE)
    {
      if (!size)
        MappingSize = MaximalWindow;
      else
        MappingSize = size;

      MapHandle = ::CreateFileMapping(
        File
        , nullptr
        , ReadOnly ? PAGE_READONLY : PAGE_READWRITE
        , HIDW(MappingSize)
        , LODW(MappingSize)
        , name
      );

      if (MapHandle != nullptr)
        hr = Window.Open(MapHandle, MappingSize, ReadOnly, MaximalWindow);
      else
      {
        int err = GetLastError();
        hr = HRESULT_FROM_WIN32(err);

        LogmeE("CreateFileMapping failed. Error: %s", OSERR(err));
      }
    }
    else
    {
      int err = GetLastError();
      hr = HRESULT_FROM_WIN32(err);

      LogmeE("CreateFile failed. Error: %s", OSERR(err));
    }
  }
  else
    hr = E_POINTER;

  if (FAILED(hr))
    Close();

  return hr;
}

HResult Mapping::Resize(uint64_t size, PCString name)
{
  HResult hr = E_FAIL;
  auto guard = Lock.Lock();

  if (MapHandle && !Window.GetReferenceCount())
  {
    Window.Close();

    if (MapHandle)
    {
      ::CloseHandle(MapHandle);
      MapHandle = nullptr;
    }

    MappingSize = size;

    MapHandle = ::CreateFileMapping(
      File
      , nullptr
      , ReadOnly ? PAGE_READONLY : PAGE_READWRITE
      , HIDW(MappingSize)
      , LODW(MappingSize)
      , name
    );

    if (MapHandle != nullptr)
      hr = Window.Open(MapHandle, MappingSize, ReadOnly, MaximalWindow);
    else
    {
      int err = GetLastError();
      hr = HRESULT_FROM_WIN32(err);

      LogmeE("CreateFileMapping failed. Error: %s", OSERR(err));
    }
  }

  return hr;
}

void Mapping::Close()
{
  auto guard = Lock.Lock();

  Window.Close();

  if (MapHandle)
  {
    if (!::CloseHandle(MapHandle))
      LogmeE("CloseHandle failed. Error: %s", OSERR2);

    MapHandle = nullptr;
  }

  if (File != INVALID_HANDLE_VALUE)
  {
    if (!::CloseHandle(File))
      LogmeE("CloseHandle failed. Error: %s", OSERR2);

    File = INVALID_HANDLE_VALUE;
  }

  MappingSize = 0;
}

PVOID Mapping::Map(uint64_t offset, uint64_t size)
{
  auto guard = Lock.Lock();

  return Window.Map(offset, size);
}

void Mapping::Unmap(PVOID address)
{
  Window.Unmap(address);
}

#endif
