#pragma once

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

#include <Syncme/Api.h>

namespace Syncme
{
  namespace Sockets
  {
    namespace IO
    {
      constexpr static size_t LIMIT = 64ULL * 1024;
      constexpr static size_t BUFFER_SIZE = 64ULL * 1024;
      constexpr static size_t KEEP_BUFFERS = 4;

      typedef std::vector<char> Buffer;
      typedef std::shared_ptr<Buffer> BufferPtr;
      typedef std::list<BufferPtr> BufferList;
      typedef std::function<void()> TSignalTxReady;

      class Queue
      {
        size_t Limit;
        TSignalTxReady Signal;

        std::mutex Lock;
        BufferList Packets;
        BufferList Free;
        size_t Total;

      public:
        SINCMELNK Queue(size_t limit = LIMIT, TSignalTxReady signal = TSignalTxReady());
        SINCMELNK void SetSignallReady(TSignalTxReady signal);

        SINCMELNK bool Append(BufferPtr buffer);
        SINCMELNK bool Append(const void* p, size_t cb);
        SINCMELNK bool IsEmpty() const;
        SINCMELNK size_t Size() const;

        SINCMELNK const void* Join(size_t& cb);
        SINCMELNK const void* FirstItem(size_t& cb);
        SINCMELNK void RemoveFirst();

      private:
        BufferPtr PopFree();
        void PushFree(BufferPtr b);
      };
    }
  }
}