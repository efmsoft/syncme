#include <utility>

#include <Syncme/Sockets/Async/BufferChain.h>

using namespace Syncme::Sockets::Async;

BufferChain::BufferChain()
  : TotalSize(0)
{
}

bool BufferChain::Add(
  Syncme::Sockets::IO::BufferPtr buffer
  , size_t offset
  , size_t size
)
{
  if (buffer == nullptr || size == 0)
    return false;

  if (offset > buffer->size() || size > buffer->size() - offset)
    return false;

  Views.emplace_back(std::move(buffer), offset, size);
  TotalSize += size;
  return true;
}

bool BufferChain::Add(Syncme::Sockets::IO::BufferPtr buffer)
{
  if (buffer == nullptr)
    return false;

  return Add(buffer, 0, buffer->size());
}

void BufferChain::Clear()
{
  Views.clear();
  TotalSize = 0;
}

bool BufferChain::IsEmpty() const
{
  return TotalSize == 0;
}

size_t BufferChain::Size() const
{
  return TotalSize;
}

const std::vector<BufferView>& BufferChain::GetViews() const
{
  return Views;
}
