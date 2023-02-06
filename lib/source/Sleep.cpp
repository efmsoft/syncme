#include <Syncme/Sleep.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <sys/time.h>
  #include <time.h>
#endif

void Syncme::Sleep(unsigned millisec)
{
#ifdef _WIN32
  return ::Sleep(millisec);  
#else
  timespec delay;
  timespec remain;
  unsigned usec = (millisec % 1000) * 1000;
  delay.tv_sec = millisec / 1000 + usec / 1000000;
  delay.tv_nsec = (usec % 1000000) * 1000;
  while (nanosleep(&delay, &remain) != 0)
  {
    delay = remain;
  }
#endif  
} 
