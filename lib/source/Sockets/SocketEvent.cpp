#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/Counter.h>
#include <Syncme/Sockets/SocketEvent.h>
#include <Syncme/Sockets/SocketEventQueue.h>

using namespace Syncme::Implementation;

#define SIGNATURE *(uint32_t*)"SktE";

std::atomic<uint64_t> Syncme::SocketEventObjects{};

SocketEvent::SocketEvent(int socket, int mask)
  : Event(false)
  , Socket(socket)
  , EventMask(mask)
  , Events(0)
#ifdef _WIN32
  , UnregisterDone(::CreateEventA(nullptr, true, false, nullptr))
  , WSAEvent(WSACreateEvent())
  , WaitObject(nullptr)
#endif
{
  assert(socket != -1);

  int notValidBits = ~(EVENT_READ | EVENT_WRITE | EVENT_CLOSE);
  assert(!(mask & notValidBits));

#ifdef _WIN32
  long events = 0;

  if (EventMask & EVENT_READ)
    events |= FD_READ;

  if (EventMask & EVENT_WRITE)
    events |= FD_WRITE;

  if (EventMask & EVENT_CLOSE)
    events |= FD_CLOSE;

  if (WSAEventSelect((SOCKET)Socket, WSAEvent, events))
  {
    LogEwsa("WSAEventSelect failed");
  }
  else
  {
    auto f = RegisterWaitForSingleObject(
      &WaitObject
      , WSAEvent
      , &WaitOrTimerCallback
      , this
      , INFINITE
      , WT_EXECUTEDEFAULT
    );

    if (!f)
    {
      LogEwsa("RegisterWaitForSingleObject failed");
    }
  }
#endif

  SocketEventObjects++;
}

SocketEvent::~SocketEvent()
{
  if (true)
  {
    auto guard = EventLock.Lock();
    EventMask = 0;
  }

#ifdef _WIN32
  if (WaitObject)
  {
    // MSDN:  If the function succeeds, or if the function fails with ERROR_IO_PENDING, 
    // the caller should always wait until the event is signaled to close the event. 
    // If the function fails with a different error code, it is not necessary to wait 
    // until the event is signaled to close the event.
    BOOL f = ::UnregisterWaitEx(WaitObject, UnregisterDone);
    if (!f && ::GetLastError() == ERROR_IO_PENDING)
      f = true;

    if (!f)
    {
      LogEwsa("UnregisterWaitEx failed");
    }
    else
    {
      auto rc = ::WaitForSingleObject(UnregisterDone, INFINITE);
      assert(rc == WAIT_OBJECT_0);
    }

    WaitObject = nullptr;
  }

  if (WSAEvent)
  {
    auto f = WSACloseEvent(WSAEvent);
    assert(f);

    WSAEvent = nullptr;
  }

  if (UnregisterDone)
  {
    auto f = ::CloseHandle(UnregisterDone);
    assert(f);

    UnregisterDone = nullptr;
  }
#endif

  SocketEventObjects--;
}

void SocketEvent::OnCloseHandle()
{
  // With a null mask, the event is guaranteed not to be added to queued!
  EventMask = 0;

#ifndef _WIN32
  auto guard = SocketEventQueue::RemoveLock.Lock();
  auto& queue = SocketEventQueue::Ptr();

  if (queue)
    queue->RemoveSocketEvent(this);

  if (queue->Empty())
    queue.reset();
#endif

  Event::OnCloseHandle();
}

void SocketEvent::Update()
{
  auto guard = EventLock.Lock();

  if (EventMask & EVENT_READ)
  {
    unsigned long n = 0;
    if (ioctlsocket(Socket, FIONREAD, &n) == 0)
    {
      if (n)
        Events |= EVENT_READ;
      else
        Events &= ~EVENT_READ;
    }
  }

  if (Events)
    SetEvent(this);
  else
    ResetEvent(this);
}

bool SocketEvent::Wait(uint32_t ms)
{
  Update();
  return Event::Wait(ms);
}

uint32_t SocketEvent::RegisterWait(TWaitComplete complete)
{
  Update();
  return Event::RegisterWait(complete);
}

#ifndef _WIN32
epoll_event SocketEvent::GetPollEvent() const
{
  epoll_event ev{};
  ev.data.fd = Socket;

  if (EventMask & EVENT_READ)
    ev.events |= EPOLLIN;

  if (EventMask & EVENT_WRITE)
    ev.events |= EPOLLOUT;

  if (EventMask & EVENT_CLOSE)
    ev.events |= EPOLLRDHUP;

  return ev;
}
#endif

int SocketEvent::GetEvents()
{
  int e = 0;
  bool reg = false;

  if (true)
  {
    auto guard = EventLock.Lock();

    e = Events;
    Events = 0;

    if (e & EVENT_CLOSE)
    {
      EventMask &= ~EVENT_CLOSE;
      reg = true;
    }
  }

  if (reg)
  {
#ifndef _WIN32
    // Update flags in poll queue
    auto guard = SocketEventQueue::RemoveLock.Lock();
    auto& queue = SocketEventQueue::Ptr();

    if (queue)
    {
      if (queue->RemoveSocketEvent(this))
        queue->AddSocketEvent(this);
    }
#endif    
  }
  return e;
}

uint32_t SocketEvent::Signature() const
{
  return SIGNATURE;
}

bool SocketEvent::IsSocketEvent(HEvent h)
{
  if (h == nullptr)
    return false;

  return h->Signature() == SIGNATURE;
}

void SocketEvent::FireEvents(int events)
{
  if ((events & EventMask) == 0)
    return;

  do
  {
    auto guard = EventLock.Lock();
    Events = events & EventMask;

  } while (false);

  SetEvent(this);
}

#ifdef _WIN32
void CALLBACK SocketEvent::WaitOrTimerCallback(
  void* lpParameter
  , unsigned char timerOrWaitFired
)
{
  SocketEvent* self = (SocketEvent*)lpParameter;
  self->Callback(timerOrWaitFired != 0);
}

void SocketEvent::Callback(bool timerOrWaitFired)
{
  assert(timerOrWaitFired == false);

  do
  {
    auto guard = EventLock.Lock();
    if (EventMask == 0)
      break;

    WSANETWORKEVENTS events{};
    int e = WSAEnumNetworkEvents(
      (SOCKET)Socket
      , WSAEvent
      , &events
    );

    if (e)
    {
      LogEwsa("WSAEnumNetworkEvents failed");
    }
    else
    {
      if ((events.lNetworkEvents & FD_READ) && (EventMask & EVENT_READ))
        Events |= EVENT_READ;

      if ((events.lNetworkEvents & FD_WRITE) && (EventMask & EVENT_WRITE))
        Events |= EVENT_WRITE;

      if ((events.lNetworkEvents & FD_CLOSE) && (EventMask & EVENT_CLOSE))
        Events |= EVENT_CLOSE;

      if (Events)
        SetEvent(this);
    }
  } while (false);
}
#endif
