#include <Syncme/Sync.h>

#if CS_USE_CRITICAL_SECTION
#include <windows.h>
#endif

// Must be included after windows.h !!!
#include <Syncme/CritSection.h>
#include <Syncme/ProcessThreadId.h>
#include <Syncme/TickCount.h>

using namespace Syncme;

CS::AutoLock::AutoLock(CS* section, bool tryLock)
  : Section(section)
{
  if (tryLock)
  {
    if (Section->TryAcquire() == false)
      Section = nullptr;
  }
  else
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

CS::AutoLock::operator bool() const
{
  return Section != nullptr;
}

CS::CS()
#if CS_USE_CRITICAL_SECTION
  : SectionData{}
#endif  
{
#ifdef CS_DETECT_LOCKS
  MaxWait = INFINITE;
  MutexHandle = ::CreateMutex(nullptr, false, nullptr);
  OwningThread = 0;
#endif

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
#ifdef CS_DETECT_LOCKS
  ::CloseHandle(MutexHandle);
#endif

#if CS_USE_CRITICAL_SECTION
  DeleteCriticalSection(&CriticalSection);
#endif
}

void CS::SetMaxWait(int n)
{
#ifdef CS_DETECT_LOCKS
  MaxWait = n;
#endif
}

const CS::AutoLock CS::TryLock()
{
  return AutoLock(this, true);
}

const CS::AutoLock CS::Lock()
{
  return AutoLock(this, false);
}

bool CS::TryAcquire()
{
#if CS_USE_CRITICAL_SECTION
  if (TryEnterCriticalSection(&CriticalSection))
    return true;

  return false;
#else
  if (Mutex.try_lock())
  {
    OwningThread = GetCurrentThreadId(true);
    return true;
  }
  return false;
#endif
}

void CS::Acquire()
{
#ifdef CS_DETECT_LOCKS
  do
  {
    auto rc = ::WaitForSingleObject(MutexHandle, MaxWait);
    if (rc == WAIT_TIMEOUT)
    {
      DebugBreak();
      continue;
    }

    if (rc != WAIT_OBJECT_0)
    {
      DebugBreak();
    }
    
    OwningThread = GetCurrentThreadId(true);

  } while (false);
#else
  #if CS_USE_CRITICAL_SECTION
    EnterCriticalSection(&CriticalSection);
  #else
    Mutex.lock();
    OwningThread = GetCurrentThreadId();
  #endif
#endif
}

void CS::Release()
{
#ifdef CS_DETECT_LOCKS
  ::ReleaseMutex(MutexHandle);
#else
  #if CS_USE_CRITICAL_SECTION
    LeaveCriticalSection(&CriticalSection);
  #else
    Mutex.unlock();
  #endif
#endif
}
