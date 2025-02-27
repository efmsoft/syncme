#ifdef _WIN32
#include <cassert>
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

void Socket::EventSelect(IOStat& stat)
{
  int mask = EVENT_READ;

  if (Peer.Disconnected == false)
    mask |= EVENT_CLOSE;

  if (TxQueue.IsEmpty() == false)
    mask |= EVENT_WRITE;

  SocketEvent* socketEvent = (SocketEvent*)RxEvent.get();
  if (mask == socketEvent->GetMask())
    return;

#if SKTIODEBUG
  if (mask == (EVENT_READ))
  {
    IODEBUGS("sel", "r");
  }
  else if (mask == (EVENT_READ | EVENT_CLOSE))
  {
    IODEBUGS("sel", "rc");
  }
  else if (mask == (EVENT_READ | EVENT_WRITE | EVENT_CLOSE))
  {
    IODEBUGS("sel", "rwc");
  }
#endif

  socketEvent->SetMask(mask);
}

static void AppendEventName(
  char* buffer
  , const WSANETWORKEVENTS& nev
  , int flag
  , int bit
  , const char* name
)
{
  if ((nev.lNetworkEvents & flag) == 0)
    return;

  if (*buffer)
    strcat(buffer, "+");

  strcat(buffer, name);
  if (nev.iErrorCode[bit])
    sprintf(buffer + strlen(buffer), "[%i]", nev.iErrorCode[bit]);
}

bool Socket::ReadSocketEvents(IOStat& stat, HANDLE waEvent)
{
  WSANETWORKEVENTS nev{};
  if (WSAEnumNetworkEvents(Handle, waEvent, &nev))
  {
    SKT_SET_LAST_ERROR(GENERIC);
    return false;
  }

#if SKTIODEBUG
  char buffer[256]{};
  if (nev.lNetworkEvents == 0)
  {
    IODEBUG("ev", 0);
    return true;
  }

  AppendEventName(buffer, nev, FD_READ, FD_READ_BIT, "read");
  AppendEventName(buffer, nev, FD_WRITE, FD_WRITE_BIT, "write");
  AppendEventName(buffer, nev, FD_CLOSE, FD_CLOSE_BIT, "close");

  IODEBUGS("ev", buffer);
#endif

  if (nev.lNetworkEvents & FD_CLOSE)
  {
    // On next cycle after attempt to read from the 
    // socket we will break the loop

    Peer.Disconnected = true;
    Peer.When = GetTimeInMillisec();

    LogW("%s: peer disconnected", Pair->WhoAmI(this));
  }

  return true;
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
  auto loopStart = GetTimeInMillisec();
  auto callid = rand();

  memset(&stat, 0, sizeof(stat));
  SKT_SET_LAST_ERROR(NONE);

  SKTCOUNTERADD(IOFunctionCall, 1);

  SocketEvent* socketEvent = (SocketEvent*)RxEvent.get();
  HANDLE waEvent = socketEvent->GetWSAEvent();

  HANDLE object[5]{};
  object[evSocket] = waEvent;
  object[evTX] = WStartTX;
  object[evBreak] = WStopIO;
  object[evExit] = WExitEvent;
  object[evStop] = WStopEvent;

  WSANETWORKEVENTS nev{};
  for (bool expired = false;;)
  {
    auto ticks = GetTimeInMillisec();
    SKTCOUNTERADD(TotalLoopTime, ticks - loopStart);
    loopStart = ticks;

    stat.Cycles++;
    SKTCOUNTERADD(TotalIOLoops, 1);

    // Try to reset waEvent
    if (!ReadSocketEvents(stat, waEvent))
      return false;

    // If peer is disconnected, write will fail
    // We socked still can contain data to read
    if (Peer.Disconnected == false)
    {
      ::ResetEvent(WStartTX);

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

    if (timeout == 0)
      return true;

    if (flags.f.Flush && TxQueue.IsEmpty())
      return true;

    // Return immediatelly if we read at least one packet
    if (RxQueue.IsEmpty() == false && flags.f.ForceWait != true)
      return true;

    IODEBUG(nullptr, 0);

    uint32_t ms = CalculateTimeout(timeout, start, expired);
    if (expired)
    {
      SKT_SET_LAST_ERROR(TIMEOUT);
      break;
    }

    EventSelect(stat);

    DWORD rc = WAIT_TIMEOUT;

    TimePoint t0;
    rc = ::WaitForMultipleObjects(5, object, false, ms);

    auto spent = t0.ElapsedSince();
    stat.WaitTime += spent;
    stat.Wait++;

    if (spent < 100)
    {
      SKTCOUNTERADD(TotalWaitTime, spent);
      SKTCOUNTERADD(WaitCount, 1);
    }

    if (rc == WAIT_FAILED)
    {
      IODEBUG("failed", spent);
      SKT_SET_LAST_ERROR(GENERIC);
      return false;
    }

    if (rc == WAIT_TIMEOUT)
    {
      IODEBUG("timeout", spent);
      SKTCOUNTERADD(Timeouts, 1);
      SKT_SET_LAST_ERROR(TIMEOUT);
      break;
    }

    int ev = int(rc - WAIT_OBJECT_0);
    IODEBUG(EvName[ev], spent);
    SKTCOUNTERADD(Events[ev], 1);

    // WExitEvent or WStopEvent
    if (ev == evExit || ev == evStop)
    {
      SKT_SET_LAST_ERROR(CONNECTION_ABORTED);
      return false;
    }

    // WStopIO
    if (ev == evBreak)
      break;

    // WStartTX
    if (ev == evTX)
      continue;
  }

  return true;
}

#endif // #ifdef _WIN32
