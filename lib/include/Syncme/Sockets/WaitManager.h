#pragma once

#include <Syncme/Api.h>
namespace Syncme
{
  namespace Implementation
  {
    struct SocketEvent;

    namespace WaitManager
    {
      SINCMELNK void AddSocketEvent(SocketEvent* e);
      SINCMELNK void RemoveSocketEvent(SocketEvent* e);
      SINCMELNK void Uninitialize();
    };
  }
}