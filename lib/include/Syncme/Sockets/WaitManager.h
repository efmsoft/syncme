#pragma once

namespace Syncme
{
  namespace Implementation
  {
    struct SocketEvent;

    namespace WaitManager
    {
      void AddSocketEvent(SocketEvent* e);
      void RemoveSocketEvent(SocketEvent* e);
      void Uninitialize();
    };
  }
}