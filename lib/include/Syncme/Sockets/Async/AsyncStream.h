#pragma once

#include <cstddef>
#include <cstdint>
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

      // Where the code of an Operation::Error result comes from. A socket
      // stream reports an OS error of its socket (Transport); a TLS stream
      // reports either an SSL_ERROR_* value (Tls) or the OS error of the
      // transport under it (Transport). The two code spaces overlap
      // (SSL_ERROR_SYSCALL == EIO on Linux), so the code alone cannot tell.
      enum class ErrorOrigin
      {
        None,
        Transport,
        Tls
      };

      struct Result
      {
        AsyncStreamPtr Stream;
        void* Context;
        Operation Op;
        IO::BufferPtr Buffer;
        size_t Bytes;
        int Error;
        ErrorOrigin Origin;

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

        // True only when progress is observable during a pending write.
        // Whole-operation completion alone is insufficient for stall checks.
        SINCMELNK virtual bool CanTrackWriteProgress() const;

        // Uses the GetTimeInMillisec clock. Updated only when bytes are
        // written to the transport, including partial writes. TLS exposes
        // the underlying transport's progress. Zero means no progress yet.
        SINCMELNK virtual uint64_t GetLastWriteProgressTime() const;
      };

      class AsyncEngine
      {
      public:
        SINCMELNK virtual ~AsyncEngine();

        SINCMELNK static std::unique_ptr<AsyncEngine> Create();

        SINCMELNK virtual bool IsValid() const = 0;

        SINCMELNK virtual bool Add(
          Socket* socket
          , void* context
          , AsyncStreamPtr& stream
        ) = 0;

        // Keeps the native registration and pending I/O, but changes the
        // Socket wrapper used by an already registered stream.
        SINCMELNK virtual bool RebindSocket(
          AsyncStream* stream
          , Socket* socket
        ) = 0;

        SINCMELNK virtual bool Remove(AsyncStream* stream) = 0;
        SINCMELNK virtual bool Wait(Result& result, int timeout) = 0;

        // A result the engine already holds, the very one Wait() would hand
        // out before it looked at the system at all; false, with `result`
        // empty, when there is none. It never waits and never makes a system
        // call, so a caller that has just dealt with one result can take the
        // ones queued behind it without going round its own loop for each.
        // An engine that keeps no queue of its own has nothing to give: the
        // default.
        SINCMELNK virtual bool TryPopPendingResult(Result& result);

        SINCMELNK virtual void Wake() = 0;
        SINCMELNK virtual void Stop() = 0;
      };
    }
  }
}
