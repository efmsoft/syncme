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
  if (Handle == -1)
    return false;

  IOFlags flags{};
  flags.f.Flush = true;

  IOStat stat{};
  return IO(timeout, stat, flags);
}

bool Socket::WriteIO(IOStat& stat)
{
  TimePoint t0;

  for (;;)
  {
    auto b = TxQueue.PopFirst();
    if (b == nullptr)
      break;

    size_t size = b->size();
    int n = InternalWrite(b->data(), size, 0);

    if (n > 0)
    {
      LogmeI("sent %zu bytes to %s (%zu)", size, Pair->WhoAmI(this), TxQueue.Size());
      TxQueue.PushFree(b);

      stat.Sent += size;
      stat.SentPkt++;

      continue;
    }

    if (b->size())
    {
      LogmeE("failed to send %zu bytes to %s", size, Pair->WhoAmI(this));
      TxQueue.PushFront(b);
    }
    else
      TxQueue.PushFree(b);

    if (n < 0)
    {
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
    if (n > 0)
    {
      stat.Rcv += n;
      stat.RcvPkt++;

      RxQueue.Append(RxBuffer, n);
      LogmeI("queued %i bytes from %s (%zu)", n, Pair->WhoAmI(this), RxQueue.Size());
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

int Socket::WriteStr(const std::string& str, int timeout)
{
  return Write(str.c_str(), str.length(), timeout);
}

int Socket::Write(const std::vector<char>& arr, int timeout)
{
  return Write(&arr[0], arr.size(), timeout);
}

int Socket::Write(const void* buffer, size_t size, int timeout)
{
  if (TxQueue.Append(buffer, size) == false)
  {
    SKT_SET_LAST_ERROR(WOULDBLOCK);
    return 0;
  }
  
  SKT_SET_LAST_ERROR(NONE);
  return size;
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
