#ifdef _WIN32
#include <winsock2.h>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/Socket.h>
#include <Syncme/Sockets/SocketEvent.h>
#include <Syncme/Sockets/SocketPair.h>
#include <Syncme/TickCount.h>

using namespace Syncme;
using namespace Syncme::Implementation;
using namespace std::placeholders;

void Socket::InitWin32Events()
{
  WExitEvent = CreateEvent(nullptr, true, false, nullptr);
  WStopEvent = CreateEvent(nullptr, true, false, nullptr);
  WStopIO = CreateEvent(nullptr, false, false, nullptr);
  WStartTX = CreateEvent(nullptr, false, false, nullptr);

  ExitEventCookie = Pair->GetExitEvent()->RegisterWait(
    std::bind(&Socket::SignallWindowsEvent, this, WExitEvent, _1, _2)
  );

  CloseEventCookie = Pair->GetCloseEvent()->RegisterWait(
    std::bind(&Socket::SignallWindowsEvent, this, WStopEvent, _1, _2)
  );

  StartTXEventCookie = StartTX->RegisterWait(
    std::bind(&Socket::SignallWindowsEvent, this, WStartTX, _1, _2)
  );
}

void Socket::FreeWin32Events()
{
  if (ExitEventCookie)
  {
    Pair->GetExitEvent()->UnregisterWait(ExitEventCookie);
    ExitEventCookie = 0;
  }
  
  if (StartTXEventCookie)
  {
    StartTX->UnregisterWait(StartTXEventCookie);
    StartTXEventCookie = 0;
  }

  if (WExitEvent)
  {
    ::CloseHandle(WExitEvent);
    WExitEvent = nullptr;
  }

  if (WStopEvent)
  {
    ::CloseHandle(WStopEvent);
    WStopEvent = nullptr;
  }

  if (WStopIO)
  {
    ::CloseHandle(WStopIO);
    WStopIO = nullptr;
  }

  if (WStartTX)
  {
    ::CloseHandle(WStartTX);
    WStartTX = nullptr;
  }
}

void Socket::SignallWindowsEvent(
  void* h
  , uint32_t cookie
  , bool failed
)
{
  ::SetEvent(h);
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

  SocketEvent* socketEvent = (SocketEvent*)RxEvent.get();
  HANDLE waEvent = socketEvent->GetWSAEvent();

  HANDLE object[5]{};
  object[0] = WExitEvent;
  object[1] = WStopEvent;
  object[2] = waEvent;
  object[3] = WStopIO;
  object[4] = WStartTX;

  for (bool expired = false;;)
  {
    stat.Cycles++;

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
    if (RxQueue.IsEmpty() == false && flags.f.ForceWait != true)
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
    auto rc = ::WaitForMultipleObjects(5, object, false, timeout);
    stat.WaitTime += t0.ElapsedSince();
    stat.Wait++;

    if (rc == WAIT_TIMEOUT)
    {
      SKT_SET_LAST_ERROR(TIMEOUT);
      break;
    }

    // WExitEvent or WStopEvent
    if (rc == WAIT_OBJECT_0 || rc == WAIT_OBJECT_0 + 1)
    {
      SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      return false;
    }

    // WStopIO
    if (rc == WAIT_OBJECT_0 + 3)
      break;

    // WStartTX
    if (rc == WAIT_OBJECT_0 + 4)
      continue;

    WSANETWORKEVENTS nev{};
    if (WSAEnumNetworkEvents(Handle, waEvent, &nev))
    {
      SKT_SET_LAST_ERROR(GENERIC);
      return false;
    }

    if (nev.lNetworkEvents & FD_CLOSE)
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

#endif // #ifdef _WIN32
