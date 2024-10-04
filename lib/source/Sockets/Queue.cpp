#include <cassert>
#include <string.h>

#include <Syncme/Sockets/Queue.h>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace Syncme;
using namespace Syncme::Sockets::IO;

Queue::Queue(size_t limit, TSignalTxReady signal)
  : Limit(limit)
  , Signal(signal)
  , Total(0)
{
}

void Queue::SetSignallReady(TSignalTxReady signal)
{
  Signal = signal;
}

BufferPtr Queue::PopFree()
{
  std::lock_guard guard(Lock);
  
  if (Free.empty())
    return BufferPtr();

  BufferPtr b = Free.front();
  Free.pop_front();

  return b;
}

void Queue::PushFree(BufferPtr b)
{
  if (b->size() <= BUFFER_SIZE)
  {
    if (Free.size() < KEEP_BUFFERS)
    {
      std::lock_guard guard(Lock);

      Free.push_back(b);
    }
  }
}

bool Queue::Append(BufferPtr buffer)
{
  if (buffer == nullptr)
    return false;

  if (true)
  {
    std::lock_guard guard(Lock);

    if (Total + buffer->size() > Limit)
      return false;

    Packets.push_back(buffer);
    Total += buffer->size();
  }

  if (Signal)
    Signal();

  return true;
}

bool Queue::Append(const void* p, size_t cb)
{
  assert(p);
  assert(cb);
  assert(cb <= BUFFER_SIZE);

  if (Total + cb > Limit)
    return false;

  BufferPtr b = PopFree();
  
  if (b == nullptr)
  {
    b = std::make_shared<Buffer>();
    if (b == nullptr)
      return false;

    b->reserve(BUFFER_SIZE);
  }

  b->resize(cb);
  memcpy(&(*b)[0], p, cb);

  if (true)
  {
    std::lock_guard guard(Lock);

    Packets.push_back(b);
    Total += cb;
  }

  if (Signal)
    Signal();

  return true;
}

bool Queue::IsEmpty() const
{
  return Total == 0;
}

size_t Queue::Size() const
{
  return Total;
}

const void* Queue::Join(size_t& cb)
{
  std::lock_guard guard(Lock);
  
  if (Packets.size() == 0)
  {
    cb = 0;
    return nullptr;
  }

  if (Packets.size() == 1)
  {
    auto& b = Packets.front();
    
    cb = b->size();
    return b->data();
  }

  auto& b = Packets.front();
    
  size_t pos = b->size();
  b->resize(Total);

  for (auto it = Packets.begin(); it != Packets.end();)
  {
    if (it == Packets.begin())
    {
      ++it;
      continue;
    }

    BufferPtr b1 = *it;
    memcpy(&(*b)[pos], b1->data(), b1->size());
    pos += b1->size();

    it = Packets.erase(it);
  }

  cb = Total;
  return b->data();
}

const void* Queue::FirstItem(size_t& cb)
{
  std::lock_guard guard(Lock);

  if (Packets.empty())
  {
    assert(Total == 0);

    cb = 0;
    return nullptr;
  }

  auto& b = Packets.front();
  
  cb = b->size();
  return b->data();
}

void Queue::RemoveFirst()
{
  BufferPtr b;

  if (true)
  {
    std::lock_guard guard(Lock);

    assert(Total);
    assert(Packets.size());

    b = Packets.front();
    Total -= b->size();

    if (int64_t(Total) < 0)
      DebugBreak();

    Packets.pop_front();
  }

  PushFree(b);
}
