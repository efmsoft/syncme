#include <utility>

#include <Syncme/Sockets/Async/AsyncWriteQueue.h>
#include <Syncme/Sockets/Async/Counter.h>

using namespace Syncme::Sockets::Async;
namespace IO = Syncme::Sockets::IO;

namespace
{
  void UpdatePeak(
    std::atomic<uint64_t>& peak
    , uint64_t value
  )
  {
    uint64_t current = peak.load(std::memory_order_relaxed);
    while (current < value
      && !peak.compare_exchange_weak(
        current
        , value
        , std::memory_order_relaxed
      ))
    {
    }
  }

  void AddWriteQueueBytes(size_t bytes)
  {
    if (bytes == 0)
      return;

    uint64_t value = AsyncWriteQueueBytes.fetch_add(
      uint64_t(bytes)
      , std::memory_order_relaxed
    ) + uint64_t(bytes);

    UpdatePeak(AsyncWriteQueueBytesPeak, value);
  }

  void RemoveWriteQueueBytes(size_t bytes)
  {
    if (bytes != 0)
      AsyncWriteQueueBytes.fetch_sub(uint64_t(bytes), std::memory_order_relaxed);
  }
}

AsyncWriteQueue::AsyncWriteQueue()
  : QueuedBytes(0)
  , WritePending(false)
{
}

AsyncWriteQueue::~AsyncWriteQueue()
{
  Clear();
}

void AsyncWriteQueue::Attach(AsyncStreamPtr stream)
{
  Stream = stream;
}

void AsyncWriteQueue::Detach()
{
  Stream.reset();
  Clear();
}

bool AsyncWriteQueue::Push(const BufferChain& buffers)
{
  if (buffers.IsEmpty())
    return true;

  return Push(BufferChain(buffers));
}

bool AsyncWriteQueue::Push(BufferChain&& buffers)
{
  if (buffers.IsEmpty())
    return true;

  const size_t size = buffers.Size();

  Queue.push_back(std::move(buffers));
  QueuedBytes += size;
  AddWriteQueueBytes(size);

  return StartNext();
}

bool AsyncWriteQueue::Push(IO::BufferPtr buffer)
{
  BufferChain chain;
  if (!chain.Add(std::move(buffer)))
    return false;

  return Push(std::move(chain));
}

bool AsyncWriteQueue::OnWriteCompleted(size_t bytes, BufferChain* completed)
{
  if (!WritePending || Queue.empty())
    return false;

  const size_t size = Queue.front().Size();
  if (bytes != size)
    return false;

  if (completed)
    *completed = std::move(Queue.front());

  Queue.pop_front();
  QueuedBytes -= size;
  RemoveWriteQueueBytes(size);
  WritePending = false;

  return StartNext();
}

void AsyncWriteQueue::Clear()
{
  RemoveWriteQueueBytes(QueuedBytes);
  Queue.clear();
  QueuedBytes = 0;
  WritePending = false;
}

bool AsyncWriteQueue::IsEmpty() const
{
  return Queue.empty();
}

bool AsyncWriteQueue::IsWriting() const
{
  return WritePending;
}

bool AsyncWriteQueue::IsIdle() const
{
  return Queue.empty() && !WritePending;
}

size_t AsyncWriteQueue::Size() const
{
  return QueuedBytes;
}

size_t AsyncWriteQueue::Count() const
{
  return Queue.size();
}

bool AsyncWriteQueue::StartNext()
{
  if (WritePending || Queue.empty())
    return true;

  if (Stream == nullptr)
    return false;

  WritePending = true;
  if (Stream->StartWrite(Queue.front()))
    return true;

  WritePending = false;
  return false;
}
