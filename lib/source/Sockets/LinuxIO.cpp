#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/Socket.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/TickCount.h>

using namespace Syncme;

#if SKTEPOLL

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>


void Socket::EventSignalled(WAIT_RESULT r, uint32_t cookie, bool failed)
{
  // write() will force epoll_wait to exit
  // we have to write a value > 0
  uint64_t value = uint64_t(r) + 1;
  auto s = write(EventDescriptor, &value, sizeof(value));
  if (s != sizeof(value))
  {
    LogE("write failed");
  }
}

void Socket::ResetEventObject()
{
  uint64_t value = 0;
  int n = write(EventDescriptor, &value, sizeof(value));
  if (n != sizeof(value))
  {
    LogosE("write failed");
  }
}

WAIT_RESULT Socket::FastWaitForMultipleObjects(int timeout)
{
  if (GetEventState(Pair->GetExitEvent()) == STATE::SIGNALLED)
    return WAIT_RESULT::OBJECT_0;

  if (GetEventState(Pair->GetCloseEvent()) == STATE::SIGNALLED)
    return WAIT_RESULT::OBJECT_1;

  if (GetEventState(BreakRead) == STATE::SIGNALLED)
    return WAIT_RESULT::OBJECT_3;

  if (GetEventState(StartTX) == STATE::SIGNALLED)
    return WAIT_RESULT::OBJECT_4;

  epoll_event events[2]{};
  int n = epoll_wait(Poll, &events[0], 2, timeout);
  int en = errno;

  if (n < 0)
  {
    if (en != EINTR)
    {
      LogE("epoll_wait failed. Error %i", en);
      return WAIT_RESULT::FAILED;
    }

    n = 0;
  }

  WAIT_RESULT result = WAIT_RESULT::TIMEOUT;

  for (int i = 0; i < n; ++i)
  {
    epoll_event& e = events[i];

    if (e.data.fd == EventDescriptor)
    {
      uint64_t value = 0;
      auto n = read(EventDescriptor, &value, sizeof(value));
      if (n != sizeof(value))
      {
        LogosE("read failed");
      }
      else
      {
        result = WAIT_RESULT(value - 1);
        if (result != WAIT_RESULT::OBJECT_0 && result != WAIT_RESULT::OBJECT_1)
        {
          ResetEventObject();
        }
      }
    }
    else if (e.data.fd == Handle)
    {
      if (e.events & EPOLLIN)
        EventsMask |= EVENT_READ;

      if (e.events & EPOLLOUT)
        EventsMask |= EVENT_WRITE;

      if (e.events & EPOLLRDHUP)
        EventsMask |= EVENT_CLOSE;

      result = WAIT_RESULT::OBJECT_2;
    }
  }

  return result;
}

bool Socket::IO(int timeout, IOStat& stat, IOFlags flags)
{
  std::lock_guard<std::mutex> guard(IOLock);

  if (RxEvent == nullptr)
    return false;

  auto start = GetTimeInMillisec();
  memset(&stat, 0, sizeof(stat));

  for (bool expired = false;;)
  {
    // If peer is disconnected, write will fail
    // We socked still can contain data to read
    if (Peer.Disconnected == false)
    {
      // Try to write as much as possible
      if (WriteIO(stat) == false)
        return false;
    }

    // Read as much as possible. 
    if (ReadIO(stat) == false)
      return false;

    // If peer is disconnected, we already drained socket
    if (Peer.Disconnected)
    {
      // If there are data to process, we have to return success
      return RxQueue.IsEmpty() == false;
    }

    // Return immediatelly if we read at least one packet
    if (RxQueue.IsEmpty() == false)
      return true;

    if (flags.f.Flush && TxQueue.IsEmpty())
      return true;

    uint32_t ms = CalculateTimeout(timeout, start, expired);
    if (expired)
      break;

    TimePoint t0;
    auto rc = FastWaitForMultipleObjects(timeout);
    stat.WaitTime += t0.ElapsedSince();
    stat.Wait++;

    if (rc == WAIT_RESULT::TIMEOUT)
      break;

    // WExitEvent or WStopEvent
    if (rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1)
      return false;

    // BreakRead
    if (rc == WAIT_RESULT::OBJECT_3)
    {
      ResetEvent(BreakRead);
      break;
    }

    // StartTX
    if (rc == WAIT_RESULT::OBJECT_4)
    {
      ResetEvent(StartTX);
      continue;
    }

    int events = EventsMask;
    EventsMask = 0;

    if (events & EVENT_CLOSE)
    {
      // On next cycle after attempt to read from the 
      // socket we will break the loop

      Peer.Disconnected = true;
      Peer.When = GetTimeInMillisec();

      LogW("peer disconnected");
    }

    // Other flags are not interesting for us
    // we will try both write and read in all cases
  }

  return true;
}

#endif
