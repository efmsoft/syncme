#include <Syncme/TickCount.h>

#if defined(__GNUC__)
#include <sys/time.h>
#elif defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif 

uint64_t Syncme::GetTimeInMillisec()
{
#if defined(__GNUC__)
  timeval now;
  gettimeofday(&now, 0);
  return uint64_t(now.tv_sec) * 1000 + uint64_t(now.tv_usec) / 1000;
#elif defined(_WIN32) || defined(_WIN64)
  return ::GetTickCount64();
#elif (CLOCKS_PER_SEC == 1000)
  return clock();
#else
  uint64_t clocks = clock();
  uint64_t tmp = clocks * 1000;
  if (tmp > clocks)
  {
    return tmp / CLOCKS_PER_SEC;
  }
  else
  {
    return clocks * (1000 / CLOCKS_PER_SEC);
  }
#endif
} 