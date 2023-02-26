#include <algorithm>
#include <stdint.h>

#ifdef _WIN32

#define NOMINMAX
#include <windows.h>
#include <Syncme/SetThreadName.h>

constexpr static size_t NAME_LIMIT = 512;

#elif defined(__GNUC__)

#include <pthread.h> 
#include <string.h>
#include <Syncme/SetThreadName.h>

#endif

#ifdef _WIN32

typedef struct tagTHREADNAME_INFO
{
  DWORD dwType;                            // Must be set to 0x1000
  LPCSTR szName;                           // Address of a name string in user addr space
  DWORD dwThreadID;                        // Thread ID (-1 = caller thread)
  DWORD dwFlags;                           // Reserved for future use, must be zero
} THREADNAME_INFO;

void Syncme::SetThreadName(uint64_t threadID, const char* threadName)
{
  char name[NAME_LIMIT]{};
  size_t n = std::min(NAME_LIMIT - 1, strlen(threadName));
  memset(name, 0, NAME_LIMIT);
  memcpy(name, threadName, n);

  THREADNAME_INFO info{};
  info.dwType = 0x1000;
  info.szName = name;
  info.dwThreadID = (uint32_t)threadID;
  info.dwFlags = 0;

  __try
  {
    RaiseException(0x406D1388, 0, sizeof(info) / sizeof(DWORD), (ULONG_PTR*)&info);
  }
  __except(EXCEPTION_EXECUTE_HANDLER)
  {
  }
}

void Syncme::SetThreadName(uint64_t threadID, const wchar_t* threadName)
{
  char name[NAME_LIMIT]{};
  LPSTR p = name;

  // Convert to 8 bit string. Assume that the name string contains only 
  // latin characters
  for (int iIdx = 0; *threadName && iIdx < NAME_LIMIT - 1; iIdx++)
    *p++ = (char)*threadName++;

  *p = '\0';

  THREADNAME_INFO info{};
  info.dwType = 0x1000;
  info.szName = name;
  info.dwThreadID = (uint32_t)threadID;
  info.dwFlags = 0;

  __try
  {
    RaiseException(0x406D1388, 0, sizeof(info) / sizeof(DWORD), (ULONG_PTR*)&info);
  }
  __except(EXCEPTION_EXECUTE_HANDLER)
  {
  }
}

#elif defined(__GNUC__)

void Syncme::SetThreadName(uint64_t threadID, const char* threadName)
{
  if (threadID == uint64_t(-1))
    threadID = pthread_self();

  // The thread name is a
  // meaningful C language string, whose length is restricted to 16
  // characters, including the terminating null byte ('\0').

  char name[16]{};
  strncpy(name, threadName, 15);
  pthread_setname_np(threadID, name);
}

void Syncme::SetThreadName(uint64_t threadID, const wchar_t* threadName)
{
  if (threadID == uint64_t(-1))
    threadID = pthread_self();

  char name[16]{};
  char* p = name;
  for (int iIdx = 0; *threadName && iIdx < sizeof(name) - 1; iIdx++)
    *p++ = (char)*threadName++;

  *p = '\0';
  pthread_setname_np(threadID, name);
}

#endif