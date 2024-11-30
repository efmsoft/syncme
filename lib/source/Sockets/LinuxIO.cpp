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
    LogosE("epoll: unable to set event");
  }
}

void Socket::ResetEventObject()
{
  uint64_t value = 0;
  int n = write(EventDescriptor, &value, sizeof(value));
  if (n != sizeof(value))
  {
    LogosE("epoll: unable to reset event");
  }
}

WAIT_RESULT Socket::EventStateToWaitResult()
{
  if (GetEventState(Pair->GetExitEvent()) == STATE::SIGNALLED)
    return WAIT_RESULT::OBJECT_0;

  if (GetEventState(Pair->GetCloseEvent()) == STATE::SIGNALLED)
    return WAIT_RESULT::OBJECT_1;

  if (GetEventState(BreakRead) == STATE::SIGNALLED)
    return WAIT_RESULT::OBJECT_3;

  if (GetEventState(StartTX) == STATE::SIGNALLED)
    return WAIT_RESULT::OBJECT_4;

  return WAIT_RESULT::FAILED;
}

bool Socket::UpdateEpollEventList()
{
  epoll_event ev{};
  ev.data.fd = Handle;
  ev.events = EPOLLIN | EPOLLRDHUP;

  if (TxQueue.IsEmpty() == false)
    ev.events |= EPOLLOUT;

  if (EpollMask == ev.events)
    return true;

  if (epoll_ctl(Poll, EPOLL_CTL_MOD, Handle, &ev) == -1)
  {
    LogosE("epoll: epoll_ctl(EPOLL_CTL_MOD) failed for Handle");
    return false;
  }

  EpollMask = ev.events;
  return true;
}

WAIT_RESULT Socket::FastWaitForMultipleObjects(int timeout, IOStat& stat)
{
  WAIT_RESULT result = EventStateToWaitResult();
  if (result != WAIT_RESULT::FAILED)
    return result;

  // add/remove EPOLLOUT
  if (UpdateEpollEventList() == false)
    return WAIT_RESULT::FAILED;

  int n = 0;
  epoll_event events[2]{};

  while (true)
  {
    n = epoll_wait(Poll, events, 2, timeout);

    if (n >= 0)
      break;

    int en = errno;
    if (en != EINTR)
    {
      LogosE("epoll: epoll_wait failed");
      return WAIT_RESULT::FAILED;
    }

    stat.Cycles++;
  }

  result = WAIT_RESULT::TIMEOUT;

  for (int i = 0; i < n; ++i)
  {
    epoll_event& e = events[i];

    if (e.data.fd == EventDescriptor)
    {
      uint64_t value = 0;
      auto n = read(EventDescriptor, &value, sizeof(value));
      if (n != sizeof(value))
      {
        LogosE("epoll: unable to read event state");
      }
      
      ResetEventObject();
  
      result = EventStateToWaitResult();
      if (result != WAIT_RESULT::FAILED)
        return result;
    }

    if (e.data.fd == Handle)
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
  {
    SKT_SET_LAST_ERROR(GENERIC);
    return false;
  }

  auto start = GetTimeInMillisec();

  memset(&stat, 0, sizeof(stat));
  SKT_SET_LAST_ERROR(NONE);

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
      if (RxQueue.IsEmpty())
      {
        SKT_SET_LAST_ERROR(GRACEFUL_DISCONNECT);
        return false;
      }

      return true;
    }

    // Return immediatelly if we read at least one packet
    if (RxQueue.IsEmpty() == false)
      return true;

    if (flags.f.Flush && TxQueue.IsEmpty())
      return true;

    uint32_t ms = CalculateTimeout(timeout, start, expired);
    if (expired)
    {
      SKT_SET_LAST_ERROR(TIMEOUT);
      break;
    }
    
    IODEBUG(nullptr, 0);

    TimePoint t0;
    auto rc = FastWaitForMultipleObjects(timeout, stat);
    stat.WaitTime += t0.ElapsedSince();
    stat.Wait++;

    if (rc == WAIT_RESULT::TIMEOUT)
    {
      SKT_SET_LAST_ERROR(TIMEOUT);
      break;
    }

    // WExitEvent or WStopEvent
    if (rc == WAIT_RESULT::OBJECT_0 || rc == WAIT_RESULT::OBJECT_1)
    {
      SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      return false;
    }

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

      LogW("%s: peer disconnected", Pair->WhoAmI(this));
    }

    // Other flags are not interesting for us
    // we will try both write and read in all cases
  }

  return true;
}

#endif
