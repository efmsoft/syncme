#include <cassert>

#ifdef _WIN32

#define NOMINMAX
#include <windows.h>

#include <Logme/Logme.h>
#include <Syncme/File/View.h>

using namespace Syncme::File;

uint32_t View::Granularity = 0;

View::View()
{
  if (Granularity == 0)
  {
    SYSTEM_INFO Info{};
    GetSystemInfo(&Info);

    Granularity = Info.dwAllocationGranularity;
  }

  MaximalSize = 0;
  ReadOnly = TRUE;
  MapHandle = NULL;
  Size = 0;

  Image = NULL;
  Offset = 0;
  ViewSize = 0;

  Refs = 0;
}

View::~View()
{
  Close();
}

long View::GetReferenceCount() const
{
  return Refs;
}

HResult View::Open(HANDLE h, uint64_t size, bool readonly, uint64_t max_size)
{
  HResult hr = E_FAIL;
  
  auto guard = Lock.Lock();

  assert(h);
  assert(size);
  assert(!Refs);
  assert(!Image);

  if (h && size && !Refs && !Image)
  {
    MaximalSize = (max_size | (uint64_t(Granularity) - 1)) + 1;
    ReadOnly = readonly;
    MapHandle = h;
    Size = size;
    hr = S_OK;
  }

  return hr;
}

void View::Close()
{
  auto guard = Lock.Lock();
  assert(!Refs);

  if (Image)
  {
    auto f = UnmapViewOfFile(Image);
    if (!f)
    {
      LogmeE("UnmapViewOfFile(Image) failed. Error: %s", OSERR2);
      return;
    }

    Image = nullptr;
  }

  MaximalSize = 0;
  ReadOnly = true;
  MapHandle = nullptr;
  Size = 0;
  Offset = 0;
  ViewSize = 0;
  Refs = 0;
}

void* View::Map(uint64_t offset, uint64_t size)
{
  void* ret = nullptr;
  auto guard = Lock.Lock();

  assert(MapHandle);

  if (MapHandle)
  {
    if (!size)
    {
      LogmeE("size is 0");
    }
    else if (offset >= Size)
    {
      LogmeE("offset >= Size");
    }
    else if (offset + size > Size)
    {
      LogmeE("offset + size > Size");
    }
    else if (size > std::min(Size, MaximalSize))
    {
      LogmeE("size > std::min(Size, MaximalSize)");
    }
    else
    {
      if (Image && offset >= Offset && offset + size <= Offset + ViewSize)
      {
        auto offs = (DWORD_PTR)(offset - Offset);
        ret = LPBYTE(Image) + offs;
      }

      if (!ret && !Refs)
      {
        Offset = (offset / Granularity) * Granularity;
        ViewSize = std::min(Size - Offset, MaximalSize);

        if (Image)
        {
          if (!UnmapViewOfFile(Image))
            LogmeE("UnmapViewOfFile failed. Error: %s", OSERR2);
        }

        assert(HIDW(ViewSize) == 0);

        Image = MapViewOfFile(
          MapHandle
          , ReadOnly ? FILE_MAP_READ : FILE_MAP_WRITE
          , HIDW(Offset)
          , LODW(Offset)
          , LODW(ViewSize)
        );
        
        if (!Image)
        {
          LogmeE("MapViewOfFile failed. Error: %s", OSERR2);
        }
        else if (Image)
        {
          assert(offset >= Offset);
          assert(offset + size <= Offset + ViewSize);

          auto offs = (DWORD_PTR)(offset - Offset);
          ret = LPBYTE(Image) + offs;
        }
      }
    }
  }

  if (ret)
    InterlockedIncrement(&Refs);

  return ret;
}

void View::Unmap(void* p)
{
  if (p)
  {
    assert(Refs);

    if (Refs)
    {
      uint64_t address = (DWORD_PTR)p;
      uint64_t ulImage = (DWORD_PTR)Image;
      assert(address >= ulImage && address < ulImage + Size);

      if (address >= ulImage && address < ulImage + Size)
        InterlockedDecrement(&Refs);
    }
  }
}

HIFITEM View::Ptr2Offset(void* p) const
{
  assert(MapHandle && Image);

  HIFITEM h = 0xFFFFFFFF;

  if (MapHandle && Image)
  {
    uint64_t address = (DWORD_PTR)p;
    uint64_t image = (DWORD_PTR)Image;

    if (p >= Image && address < image + ViewSize)
    {
      uint64_t diff = LPBYTE(p) - LPBYTE(Image);
      h = HIFITEM(diff + Offset);
    }
  }
  
  return h;
}

#endif