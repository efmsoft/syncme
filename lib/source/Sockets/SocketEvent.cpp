#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sleep.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/Counter.h>
#include <Syncme/Sockets/SocketEvent.h>
#include <Syncme/Sockets/SocketEventQueue.h>
#include <Syncme/Sockets/WaitManager.h>

using namespace Syncme::Implementation;

#define SIGNATURE *(uint32_t*)"SktE";

std::atomic<uint64_t> Syncme::SocketEventObjects{};
uint64_t Syncme::GetSocketEventObjects() {return Syncme::SocketEventObjects;}

SocketEvent::SocketEvent(int socket, int mask)
  : Event(false)
  , Socket(socket)
  , EventMask(mask)
  , Events(0)
#ifdef _WIN32
  , WSAEvent(WSACreateEvent())
#else
  , Closed(false)
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

  if (!WSAEvent || WSAEventSelect((SOCKET)Socket, WSAEvent, events))
  {
    LogosE("WSAEventSelect failed");
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
  if (WSAEvent)
  {
    auto f = WSACloseEvent(WSAEvent);
    assert(f);

    WSAEvent = nullptr;
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
#else
  WaitManager::RemoveSocketEvent(this);
#endif

  Event::OnCloseHandle();
}

bool SocketEvent::Wait(uint32_t ms)
{
  WaitManager::AddSocketEvent(this);
  
  bool f = Event::Wait(ms);

  WaitManager::RemoveSocketEvent(this);
  return f;
}

uint32_t SocketEvent::RegisterWait(TWaitComplete complete)
{
#ifdef _WIN32
  if (EventMask & EVENT_READ)
  {
    unsigned long n = -1;
    if (ioctlsocket(Socket, FIONREAD, &n) > 0)
    {
      Events |= EVENT_READ;
      SetEvent(this);
    }
  }
#endif

  WaitManager::AddSocketEvent(this);
  return Event::RegisterWait(complete);
}

bool SocketEvent::UnregisterWait(uint32_t cookie)
{
  bool f = Event::UnregisterWait(cookie);

  if (f)
    WaitManager::RemoveSocketEvent(this);

  return f;
}

#ifndef _WIN32
epoll_event SocketEvent::GetPollEvent() const
{
  epoll_event ev{};
  ev.data.fd = Socket;
  ev.data.ptr = (void*)this;

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
#ifdef _WIN32
    WSANETWORKEVENTS events{};
    int rc = WSAEnumNetworkEvents(
      (SOCKET)Socket
      , WSAEvent
      , &events
    );

    if (rc)
    {
      LogosE("WSAEnumNetworkEvents failed");
    }
    else
    {
      if ((events.lNetworkEvents & FD_READ) && (EventMask & EVENT_READ))
        e |= EVENT_READ;

      if ((events.lNetworkEvents & FD_WRITE) && (EventMask & EVENT_WRITE))
        e |= EVENT_WRITE;

      if ((events.lNetworkEvents & FD_CLOSE) && (EventMask & EVENT_CLOSE))
        e |= EVENT_CLOSE;
    }
#else
    if (true)
    {
      auto guard = EventLock.Lock();
      e = Events;
      Events = 0;
    }
#endif

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

  if (true)
  {
    auto guard = EventLock.Lock();
    Events = events & EventMask;
#ifndef _WIN32
    if (Events & EVENT_CLOSE)
      Closed = true;
#endif
  }

  SetEvent(this);
}
