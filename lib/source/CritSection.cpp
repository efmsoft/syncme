#include <Syncme/Sync.h>

#if CS_USE_CRITICAL_SECTION
#include <windows.h>
#endif

// Must be included after windows.h !!!
#include <Syncme/CritSection.h>
#include <Syncme/ProcessThreadId.h>

using namespace Syncme;

CS::AutoLock::AutoLock(CS* section)
  : Section(section)
{
  Section->Acquire();
}

CS::AutoLock::AutoLock(AutoLock&& src) noexcept
{
  Section = src.Section;
  src.Section = nullptr;
}

CS::AutoLock::~AutoLock()
{
  if (Section)
    Section->Release();
}

void CS::AutoLock::Release()
{
  CS* s = nullptr;
  std::swap(s, Section);

  if (s)
    s->Release();
}

CS::CS()
#if CS_USE_CRITICAL_SECTION
  : SectionData{}
#endif  
{
#if CS_USE_CRITICAL_SECTION
  static_assert(sizeof(SectionData) >= sizeof(CRITICAL_SECTION), "SectionData size is too small");

  // MSDN: dwSpinCount
  // The spin count for the critical section object. On single - processor systems, 
  // the spin count is ignored and the critical section spin count is set to 0 (zero).
  // On multiprocessor systems, if the critical section is unavailable, the calling 
  // thread spin dwSpinCount times before performing a wait operation on a semaphore 
  // associated with the critical section. If the critical section becomes free during 
  // the spin operation, the calling thread avoids the wait operation.
  const DWORD dwSpinCount = 100;
  InitializeCriticalSectionEx(
    &CriticalSection
    , dwSpinCount
    , 0
  );
#endif
}

CS::~CS()
{
#if CS_USE_CRITICAL_SECTION
  DeleteCriticalSection(&CriticalSection);
#endif
}

const CS::AutoLock CS::Lock()
{
  return AutoLock(this);
}

void CS::Acquire()
{
#if CS_USE_CRITICAL_SECTION
  EnterCriticalSection(&CriticalSection);
#else
  Mutex.lock();
  OwningThread = GetCurrentThreadId();
#endif
}

void CS::Release()
{
#if CS_USE_CRITICAL_SECTION
  LeaveCriticalSection(&CriticalSection);
#else
  Mutex.unlock();
#endif
}
