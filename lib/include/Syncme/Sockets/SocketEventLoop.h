#pragma once

#include <cstddef>
#include <memory>

#include <Syncme/Api.h>
#include <Syncme/Sockets/Socket.h>
#include <Syncme/Sockets/SocketEvent.h>

namespace Syncme
{
  enum class SocketEventLoopOperation
  {
    None,
    Read,
    Write,
    Close,
    Wake,
    Stop
  };

  struct SocketEventLoopResult
  {
    Socket* Skt;
    void* Context;
    int Events;
    SocketEventLoopOperation Operation;
    size_t Bytes;
    int Error;

    SocketEventLoopResult()
      : Skt(nullptr)
      , Context(nullptr)
      , Events(0)
      , Operation(SocketEventLoopOperation::None)
      , Bytes(0)
      , Error(0)
    {
    }
  };

  struct SocketEventLoop
  {
    SINCMELNK virtual ~SocketEventLoop();

    SINCMELNK static std::unique_ptr<SocketEventLoop> Create();

    SINCMELNK virtual bool Add(Socket* socket, void* context, int events) = 0;
    SINCMELNK virtual bool Update(Socket* socket, int events) = 0;
    SINCMELNK virtual bool Remove(Socket* socket) = 0;
    SINCMELNK virtual bool Wait(SocketEventLoopResult& result, int timeout) = 0;
    SINCMELNK virtual void Wake() = 0;
    SINCMELNK virtual void Stop() = 0;
  };
}
