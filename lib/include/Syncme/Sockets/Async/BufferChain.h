#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include <Syncme/Api.h>
#include <Syncme/Sockets/Queue.h>

namespace Syncme
{
  namespace Sockets
  {
    namespace Async
    {
      struct BufferView
      {
        IO::BufferPtr Buffer;
        size_t Offset;
        size_t Size;

        BufferView()
          : Offset(0)
          , Size(0)
        {
        }

        BufferView(
          IO::BufferPtr buffer
          , size_t offset
          , size_t size
        )
          : Buffer(std::move(buffer))
          , Offset(offset)
          , Size(size)
        {
        }
      };

      class BufferChain
      {
        std::vector<BufferView> Views;
        size_t TotalSize;

      public:
        SINCMELNK BufferChain();

        SINCMELNK bool Add(
          IO::BufferPtr buffer
          , size_t offset
          , size_t size
        );

        SINCMELNK bool Add(IO::BufferPtr buffer);
        SINCMELNK void Clear();
        SINCMELNK bool IsEmpty() const;
        SINCMELNK size_t Size() const;
        SINCMELNK const std::vector<BufferView>& GetViews() const;
      };
    }
  }
}
