#include <Syncme/Sockets/Async/AsyncWriteQueue.h>

using namespace Syncme::Sockets::Async;
namespace IO = Syncme::Sockets::IO;

AsyncWriteQueue::AsyncWriteQueue()
  : QueuedBytes(0)
  , WritePending(false)
{
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

  Queue.push_back(buffers);
  QueuedBytes += buffers.Size();

  return StartNext();
}

bool AsyncWriteQueue::Push(IO::BufferPtr buffer)
{
  BufferChain chain;
  if (!chain.Add(buffer))
    return false;

  return Push(chain);
}

bool AsyncWriteQueue::OnWriteCompleted(size_t bytes)
{
  if (!WritePending || Queue.empty())
    return false;

  const size_t size = Queue.front().Size();
  if (bytes != size)
    return false;

  Queue.pop_front();
  QueuedBytes -= size;
  WritePending = false;

  return StartNext();
}

void AsyncWriteQueue::Clear()
{
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
