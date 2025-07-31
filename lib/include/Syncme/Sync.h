#pragma once

#include <functional>

#include <Syncme/Api.h>
#include <Syncme/Event/EventArray.h>
#include <Syncme/Event/Wait.h>

namespace Syncme
{
  enum class STATE
  {
    NOT_SIGNALLED,
    SIGNALLED,

    UNDEFINED
  };
  SINCMELNK HEvent CreateNotificationEvent(STATE state = STATE::NOT_SIGNALLED);
  SINCMELNK HEvent CreateSynchronizationEvent(STATE state = STATE::NOT_SIGNALLED);
  SINCMELNK HEvent DuplicateHandle(HEvent event);
  
  SINCMELNK bool SetEvent(HEvent event);
  SINCMELNK bool ResetEvent(HEvent event);
  SINCMELNK bool CloseHandle(HEvent& event);
  SINCMELNK STATE GetEventState(HEvent event);
  SINCMELNK bool GetEventClosed(HEvent event);

  SINCMELNK HEvent CreateManualResetTimer();
  SINCMELNK HEvent CreateAutoResetTimer();
  SINCMELNK bool SetWaitableTimer(HEvent timer, long dueTime, long period, std::function<void(HEvent)> callback);
  SINCMELNK bool CancelWaitableTimer(HEvent timer);

  constexpr static int EVENT_READ = 1;
  constexpr static int EVENT_WRITE = 2;
  constexpr static int EVENT_CLOSE = 4;
  SINCMELNK HEvent CreateSocketEvent(int socket, int eventMask, void* queue = nullptr);
  SINCMELNK int GetSocketEvents(HEvent socketEvent);
}

using HEvent = Syncme::HEvent;

// Set to 1 to force the use of the standard library in the critical section implementation. 
// A value of 0 can be useful for debugging under Windows, since in this case the CRITICAL_SECTION structure 
// contains the identifier of the thread that acquired the section. In addition, the implementation using 
// CRITICAL_SECTION is much faster on multiprocessor systems
#define CS_USE_STD 0

#if defined(_WIN32) && !CS_USE_STD
#define CS_USE_CRITICAL_SECTION 1
#else
#define CS_USE_CRITICAL_SECTION 0
#endif