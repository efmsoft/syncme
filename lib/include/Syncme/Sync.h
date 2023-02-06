#pragma once

#include <functional>

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
  HEvent CreateNotificationEvent(STATE state = STATE::NOT_SIGNALLED);
  HEvent CreateSynchronizationEvent(STATE state = STATE::NOT_SIGNALLED);
  HEvent DuplicateHandle(HEvent event);
  
  bool SetEvent(HEvent event);
  bool ResetEvent(HEvent event);
  bool CloseHandle(HEvent& event);
  STATE GetEventState(HEvent event);

  HEvent CreateManualResetTimer();
  HEvent CreateAutoResetTimer();
  bool SetWaitableTimer(HEvent timer, long dueTime, long period, std::function<void(HEvent)> callback);
  bool CancelWaitableTimer(HEvent timer);

  constexpr static int EVENT_READ = 1;
  constexpr static int EVENT_WRITE = 2;
  constexpr static int EVENT_CLOSE = 4;
  HEvent CreateSocketEvent(int socket, int eventMask);
  int GetSocketEvents(HEvent socketEvent);
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