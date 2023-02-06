#include <Syncme/ProcessThreadId.h>

#if defined(_WIN32)
#include <windows.h>

uint64_t Syncme::GetCurrentProcessId()
{
  return ::GetCurrentProcessId();
}

uint64_t Syncme::GetCurrentThreadId()
{
  return ::GetCurrentThreadId();
}
#elif defined(__APPLE__) || defined(__linux__) || defined(__sun__)

#include <pthread.h>
#include <unistd.h>

uint64_t Syncme::GetCurrentProcessId()
{
  return getpid();
}

uint64_t Syncme::GetCurrentThreadId()
{
  return (uint64_t)pthread_self();
} 
#endif