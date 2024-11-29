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
  , AutoJoin(false)
{
}

void Queue::SetAutoJoin(bool f)
{
  AutoJoin = f;
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

BufferPtr Queue::GetBuffer()
{
  BufferPtr b = PopFree();

  if (b == nullptr)
  {
    b = std::make_shared<Buffer>();
    if (b == nullptr)
      return BufferPtr();

    b->reserve(BUFFER_SIZE);
  }

  return b;
}

bool Queue::Append(BufferPtr buffer, size_t* qsize)
{
  if (qsize)
    *qsize = Total;

  if (buffer == nullptr)
    return false;

  if (true)
  {
    std::lock_guard guard(Lock);

    if (Limit != -1 && Total + buffer->size() > Limit)
      return false;

    Packets.push_back(buffer);
    Total += buffer->size();

    if (qsize)
      *qsize = Total;
  }

  if (Signal)
    Signal();

  return true;
}

bool Queue::Insert(const void* p, size_t cb, size_t* qsize)
{
  assert(p);
  assert(cb);
  assert(Limit == -1 || cb <= Limit);

  if (qsize)
    *qsize = Total;

  if (Limit != -1 && Total + cb > Limit)
    return false;

  do
  {
    BufferPtr b = GetBuffer();
    if (b == nullptr)
      return false;

    b->resize(cb);
    memcpy(&(*b)[0], p, cb);

    std::lock_guard guard(Lock);

    Packets.push_front(b);
    Total += cb;

    if (qsize)
      *qsize = Total;

  } while (false);

  if (Signal)
    Signal();

  return true;
}

bool Queue::Append(const void* p, size_t cb, size_t* qsize)
{
  assert(p);
  assert(cb);
  assert(Limit == -1 || cb <= Limit);

  if (qsize)
    *qsize = Total;

  if (Limit != -1 && Total + cb > Limit)
    return false;

  do
  {
    if (AutoJoin)
    {
      std::lock_guard guard(Lock);

      if (Packets.empty() == false)
      {
        auto& b = Packets.back();
        size_t pos = b->size();

        if (pos + cb <= BUFFER_SIZE)
        {
          b->resize(pos + cb);
          memcpy(&(*b)[pos], p, cb);
          Total += cb;

          if (qsize)
            *qsize = Total;

          break;
        }
      }
    }

    BufferPtr b = GetBuffer();
    if (b == nullptr)
      return false;

    b->resize(cb);
    memcpy(&(*b)[0], p, cb);

    std::lock_guard guard(Lock);

    Packets.push_back(b);
    Total += cb;

    if (qsize)
      *qsize = Total;

  } while (false);

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

BufferPtr Queue::Join(size_t upto)
{
  std::lock_guard guard(Lock);
  
  if (Packets.size() <= 1)
    return PopFirst();

  if (upto != -1 && Total > upto)
    return PopFirst();

  BufferPtr b = Packets.front();    
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

  return PopFirst();
}

void Queue::PushFront(BufferPtr b, bool signal)
{
  if (true)
  {
    std::lock_guard guard(Lock);

    Packets.push_front(b);
    Total += b->size();
  }

  if (signal && Signal)
    Signal();
}

BufferPtr Queue::PopFirst()
{
  std::lock_guard guard(Lock);

  if (Packets.empty())
  {
    assert(Total == 0);
    return BufferPtr();
  }

  BufferPtr b = Packets.front();
  Packets.pop_front();

  Total -= b->size();

#if defined(_WIN32) && defined(_DEBUG)
  if (int64_t(Total) < 0)
    DebugBreak();
#endif

  return b;
}
