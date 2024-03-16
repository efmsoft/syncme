#pragma once 

#include <Syncme/Event/Event.h>

namespace Syncme
{
  namespace Implementation
  {
    struct WaitableTimer : public Event
    {
      SINCMELNK WaitableTimer(bool notification_event = true);
      SINCMELNK ~WaitableTimer();

      SINCMELNK void OnCloseHandle() override;
      SINCMELNK uint32_t Signature() const override;
      SINCMELNK static bool IsTimer(HEvent h);
    };
  }
}
