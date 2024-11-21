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

        std::recursive_mutex Lock;
        BufferList Packets;
        BufferList Free;
        size_t Total;

        bool AutoJoin;

      public:
        SINCMELNK Queue(size_t limit = LIMIT, TSignalTxReady signal = TSignalTxReady());
        SINCMELNK void SetSignallReady(TSignalTxReady signal);

        SINCMELNK bool Append(BufferPtr buffer, size_t* qsize = nullptr);
        SINCMELNK bool Append(const void* p, size_t cb, size_t* qsize = nullptr);
        SINCMELNK bool Insert(const void* p, size_t cb, size_t* qsize = nullptr);
        SINCMELNK bool IsEmpty() const;
        SINCMELNK size_t Size() const;

        SINCMELNK BufferPtr Join(size_t upto = -1);
        SINCMELNK BufferPtr PopFirst();
        SINCMELNK void PushFront(BufferPtr b);

        SINCMELNK BufferPtr GetBuffer();
        SINCMELNK BufferPtr PopFree();
        SINCMELNK void PushFree(BufferPtr b);

        SINCMELNK void SetAutoJoin(bool f);
      };
    }
  }
}