#include <Syncme/ProcessThreadId.h>

#if defined(_WIN32)
#include <windows.h>

uint64_t Syncme::GetCurrentProcessId()
{
  return ::GetCurrentProcessId();
}

uint64_t Syncme::GetCurrentPThread()
{
  return ::GetCurrentThreadId();
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

uint64_t Syncme::GetCurrentPThread()
{
  return (uint64_t)pthread_self();
}

uint64_t Syncme::GetCurrentThreadId()
{
  static thread_local uint64_t tid = gettid();
  return tid;
}

#endif