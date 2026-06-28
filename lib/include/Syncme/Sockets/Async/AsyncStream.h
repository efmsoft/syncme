#pragma once

#include <cstddef>
#include <memory>

#include <Syncme/Api.h>
#include <Syncme/Sockets/Async/BufferChain.h>
#include <Syncme/Sockets/Queue.h>

namespace Syncme
{
  struct Socket;

  namespace Sockets
  {
    namespace Async
    {
      class AsyncStream;
      class AsyncEngine;

      using AsyncStreamPtr = std::shared_ptr<AsyncStream>;

      enum class Operation
      {
        None,
        Read,
        Write,
        ReadClosed,
        Handshake,
        Error,
        Wake,
        Stop
      };

      struct Result
      {
        AsyncStreamPtr Stream;
        void* Context;
        Operation Op;
        IO::BufferPtr Buffer;
        size_t Bytes;
        int Error;

        SINCMELNK Result();
      };

      class AsyncStream
      {
      public:
        SINCMELNK virtual ~AsyncStream();

        SINCMELNK virtual Socket* GetSocket() const = 0;
        SINCMELNK virtual void* GetContext() const = 0;

        SINCMELNK virtual bool StartRead(IO::BufferPtr buffer) = 0;
        SINCMELNK virtual bool StartWrite(const BufferChain& buffers) = 0;
        SINCMELNK virtual bool ShutdownSend() = 0;
        SINCMELNK virtual void Close() = 0;
      };

      class AsyncEngine
      {
      public:
        SINCMELNK virtual ~AsyncEngine();

        SINCMELNK static std::unique_ptr<AsyncEngine> Create();

        SINCMELNK virtual bool Add(
          Socket* socket
          , void* context
          , AsyncStreamPtr& stream
        ) = 0;

        SINCMELNK virtual bool Remove(AsyncStream* stream) = 0;
        SINCMELNK virtual bool Wait(Result& result, int timeout) = 0;
        SINCMELNK virtual void Wake() = 0;
        SINCMELNK virtual void Stop() = 0;
      };
    }
  }
}
