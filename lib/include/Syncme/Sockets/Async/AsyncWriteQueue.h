#pragma once

#include <cstddef>
#include <deque>

#include <Syncme/Api.h>
#include <Syncme/Sockets/Async/AsyncStream.h>
#include <Syncme/Sockets/Async/BufferChain.h>

namespace Syncme
{
  namespace Sockets
  {
    namespace Async
    {
      class AsyncWriteQueue
      {
        AsyncStreamPtr Stream;
        std::deque<BufferChain> Queue;
        size_t QueuedBytes;
        bool WritePending;
        uint64_t WriteStarted;

      public:
        SINCMELNK AsyncWriteQueue();
        SINCMELNK ~AsyncWriteQueue();

        SINCMELNK void Attach(AsyncStreamPtr stream);
        SINCMELNK void Detach();

        SINCMELNK bool Push(const BufferChain& buffers);

        // The chain is taken over, not copied: it is empty afterwards. The
        // views (and the buffers they hold) reach the stream, and then
        // OnWriteCompleted()'s `completed`, as the same objects.
        SINCMELNK bool Push(BufferChain&& buffers);
        SINCMELNK bool Push(IO::BufferPtr buffer);

        // completed, if non-null, receives the chain that just finished
        // writing (moved out of the internal queue) so the caller can
        // recycle its buffers instead of letting them go straight to the
        // allocator.
        SINCMELNK bool OnWriteCompleted(size_t bytes, BufferChain* completed = nullptr);

        SINCMELNK void Clear();
        SINCMELNK bool IsEmpty() const;
        SINCMELNK bool IsWriting() const;
        SINCMELNK bool IsIdle() const;
        SINCMELNK size_t Size() const;
        SINCMELNK size_t Count() const;

        // Remaining time without transport write progress. Returns -1 when
        // idle, disabled (timeout == 0), or partial progress is not observable;
        // returns 0 when the write has stalled.
        SINCMELNK int GetStallTimeout(uint64_t now, uint64_t timeout) const;

      private:
        bool StartNext();
      };
    }
  }
}
