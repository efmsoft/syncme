#include <cassert>
#include <string.h>

#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/Sockets/Socket.h>
#include <Syncme/TickCount.h>
#include <Syncme/TimePoint.h>

#pragma warning(disable : 6262)

using namespace Syncme;

uint32_t Socket::CalculateTimeout(int timeout, uint64_t start, bool& expired)
{
  auto t = Syncme::GetTimeInMillisec();
  uint32_t milliseconds = FOREVER;

  if (timeout != FOREVER)
  {
    if (t - start >= timeout)
    {
      expired = true;
      return 0;
    }

    expired = false;
    milliseconds = uint32_t(start + timeout - t);
  }

  return milliseconds;
}

bool Socket::Flush(int timeout)
{
  if (Handle == -1 || TxQueue.IsEmpty())
    return false;

  IOFlags flags{};
  flags.f.Flush = true;

  IOStat stat{};
  return IO(timeout, stat, flags);
}

#if SKTIODEBUG
void Socket::IoDebug(IOStat& stat, const char* op, int n, const char* tn)
{
  char buf[32]{};
  const char* pbuf = buf;

  auto l = 0;
  if (op == nullptr)
  {
    l = 1;
    pbuf = "|";
  }
  else if (tn)
  {
    l = sprintf(buf, "%s:%s", op, tn);
  }
  else
    l = sprintf(buf, "%s:%i", op, n);
  
  if (l > 0)
  {
    auto s = strlen(stat.History);
    if (s && s < sizeof(stat.History) - 2)
    {
      stat.History[s] = ' ';
      s++;
    }

    if (s + l < sizeof(stat.History) - 1)
    {
      memcpy(&stat.History[s], pbuf, size_t(l) + 1);
      s += l;
    }

    stat.History[s] = '\0';
  }
}
#endif

bool Socket::WriteIO(IOStat& stat)
{
  TimePoint t0;
  std::lock_guard lock(TxLock);

  for (;;)
  {
    auto b = TxQueue.PopFirst();
    if (b == nullptr)
      break;

    size_t size = b->size();
    int n = InternalWrite(b->data(), size, 0);
    IODEBUG("tx", n);

    if (n > 0)
    {
      if (n < size)
      {
        memmove(&(*b)[0], &(*b)[n], size - n);
        b->resize(size - n);

        TxQueue.PushFront(b, false);
      }
      else
        TxQueue.PushFree(b);

      stat.Sent += n;
      stat.SentPkt++;

      continue;
    }

    if (b->size())
    {
      TxQueue.PushFront(b, false);
    }
    else
      TxQueue.PushFree(b);

    if (n < 0)
    {
      if (FailLogged == false)
      {
        char buffer[256]{};
        sprintf(buffer, "failed to send %zu bytes to %s", size, Pair->WhoAmI(this));
        LogIoError("", buffer);

        FailLogged = true;
      }

      stat.SendTime += t0.ElapsedSince();
      return false;
    }

    break;
  }

  stat.SendTime += t0.ElapsedSince();
  return true;
}

bool Socket::ReadIO(IOStat& stat)
{
  TimePoint t0;
  
  for (;;)
  {
    int n = InternalRead(RxBuffer, Sockets::IO::BUFFER_SIZE, 0);
    IODEBUG("rx", n);

    if (n > 0)
    {
      stat.Rcv += n;
      stat.RcvPkt++;

      size_t qsize = 0;
      RxQueue.Append(RxBuffer, n, &qsize);

      continue;
    }

    if (n < 0)
    {
      stat.RcvTime += t0.ElapsedSince();
      return false;
    }

    // NONE, GRACEFUL_DISCONNECT or WOULDBLOCK
    break;
  }

  stat.RcvTime += t0.ElapsedSince();
  return true;
}

bool Socket::StopPendingRead()
{
  return SetEvent(BreakRead);
}

bool Socket::ResetPendingRead()
{
  return ResetEvent(BreakRead);
}

int Socket::Read(void* buffer, size_t size, int timeout)
{
  IOStat stat{};
  bool f = IO(timeout, stat);

  size_t cb = 0;
  auto b = RxQueue.PopFirst();
  if (b == nullptr)
    return f ? 0 : -1;

  cb = b->size();
  if (cb > size)
  {
    SKT_SET_LAST_ERROR(IO_INCOMPLETE);
    RxQueue.PushFree(b);
    return -1;
  }

  memcpy(buffer, b->data(), cb);
  RxQueue.PushFree(b);
  return int(cb);
}

int Socket::Read(std::vector<char>& buffer, int timeout)
{
  return Read(&buffer[0], buffer.size(), timeout);
}

int Socket::WriteStr(const std::string& str, int timeout, bool* queued)
{
  return Write(str.c_str(), str.length(), timeout, queued);
}

int Socket::Write(const std::vector<char>& arr, int timeout, bool* queued)
{
  return Write(&arr[0], arr.size(), timeout, queued);
}

int Socket::Write(const void* buffer, size_t size, int timeout, bool* queued)
{
  if (queued)
    *queued = false;

  if (size == 0)
    return 0;

  std::lock_guard lock(TxLock);

  if (TxQueue.IsEmpty())
  {
    int e = InternalWrite(buffer, size, timeout);
    if (e < 0 || e > 0)
      return e;
  }

  if (TxQueue.Append(buffer, size) == false)
  {
    SKT_SET_LAST_ERROR(GENERIC);
    return -1;
  }
  
  if (queued)
    *queued = true;

  SKT_SET_LAST_ERROR(NONE);
  return (int)size;
}

void Socket::Unread(const char* p, size_t n)
{
  if (n)
  {
    PacketPtr packet = std::make_shared<Packet>(n);

    auto& buffer = *packet;
    memcpy(&buffer[0], p, n);

    Packets.push_back(packet);
  }
}

int Socket::ReadPacket(void* buffer, size_t size)
{
  if (Packets.empty())
    return 0;

  SKT_SET_LAST_ERROR(NONE);

  PacketPtr p = Packets.front();
  Packets.pop_front();

  int cb = int(p->size());
  memcpy(buffer, p->data(), cb);
  return cb;
}
