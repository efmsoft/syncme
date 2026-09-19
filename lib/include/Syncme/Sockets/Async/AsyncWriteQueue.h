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

      public:
        SINCMELNK AsyncWriteQueue();

        SINCMELNK void Attach(AsyncStreamPtr stream);
        SINCMELNK void Detach();

        SINCMELNK bool Push(const BufferChain& buffers);
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

      private:
        bool StartNext();
      };
    }
  }
}
