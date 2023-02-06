#pragma once 

#include <Syncme/Event/Event.h>

namespace Syncme
{
  namespace Implementation
  {
    struct WaitableTimer : public Event
    {
      WaitableTimer(bool notification_event = true);
      ~WaitableTimer();

      void OnCloseHandle() override;
      uint32_t Signature() const override;
      static bool IsTimer(HEvent h);
    };
  }
}
