#include <algorithm>
#include <cassert>
#include <cstring>

#ifndef _WIN32
#include <fcntl.h>
#endif

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/Counter.h>
#include <Syncme/Sockets/SocketEventQueue.h>
#include <Syncme/SetThreadName.h>

using namespace Syncme;
using namespace Syncme::Implementation;

static SocketEventQueuePtr Instance;
CS SocketEventQueue::RemoveLock;

static const char* CMD_EXIT = "@exit";
static const char* CMD_GROW = "@grow";

SocketEventQueue::SocketEventQueue(int& rc, unsigned minPort, unsigned maxPort)
  : EvStop(CreateNotificationEvent())
  , EvGrowDone(CreateNotificationEvent())
#ifndef _WIN32
  , Poll(-1)
  , ControlSocket(-1)
  , Port(0)
  , Events(size_t(OPTIONS::GROW_SIZE))
#endif  
{
  rc = -1;

#ifndef _WIN32
  // epoll_create(2) — Linux manual page:
  // In the initial epoll_create() implementation, the size argument
  // informed the kernel of the number of file descriptors that the
  // caller expected to add to the epoll instance.  The kernel used
  // this information as a hint for the amount of space to initially
  // allocate in internal data structures describing events.  (If
  // necessary, the kernel would allocate more space if the caller's
  // usage exceeded the hint given in size.)  Nowadays, this hint is
  // no longer required (the kernel dynamically sizes the required
  // data structures without needing the hint), but size must still be
  // greater than zero, in order to ensure backward compatibility when
  // new epoll applications are run on older kernels.
  Poll = epoll_create(1);
  if (Poll == -1)
  {
    LogosE("epoll_create failed");
    return;
  }

  ControlSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (ControlSocket == -1)
  {
    LogosE("unable to create ControlSocket socket");
    return;
  }

  if (fcntl(ControlSocket, F_SETFL, O_NONBLOCK) == -1)
  {
    LogosE("fcntl failed for ControlSocket");
    return;
  }

  if (!Bind(minPort, maxPort))
  {
    LogmeE("Unable to bind ControlSocket");
    return;
  }

  epoll_event ev{};
  ev.data.fd = ControlSocket;
  ev.events |= EPOLLIN;

  if (epoll_ctl(Poll, EPOLL_CTL_ADD, ControlSocket, &ev) == -1)
  {
    LogosE("epoll_ctl(EPOLL_CTL_ADD) failed for ControlSocket");
    return;
  }
#endif  

  rc = 0;
}

SocketEventQueue::~SocketEventQueue()
{
  Stop();

#ifndef _WIN32
  if (ControlSocket != -1)
  {
    epoll_event ev{};
    ev.data.fd = ControlSocket;
    ev.events |= EPOLLIN;
    if (epoll_ctl(Poll, EPOLL_CTL_DEL, ControlSocket, &ev) == -1)
    {
      LogosE("epoll_ctl(EPOLL_CTL_DEL) failed for ControlSocket");
    }
  }

  if (Poll != -1)
  {
    if (close(Poll) == -1)
    {
      LogosE("close(Poll) failed");
    }

    Poll = -1;
  }

  if (ControlSocket != -1)
  {
    if (close(ControlSocket) == -1)
    {
      LogosE("close(ControlSocket) failed");
    }

    ControlSocket = -1;
  }
#endif
}

SocketEventQueuePtr& SocketEventQueue::Ptr()
{
  return Instance;
}

#ifndef _WIN32
int SocketEventQueue::Bind(unsigned minPort, unsigned maxPort)
{
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  for (unsigned port = minPort; port < maxPort; port++)
  {
    addr.sin_port = htons(port);
    if (::bind(ControlSocket, (sockaddr*)&addr, sizeof(addr)) != -1)
    {
      Port = port;
      return port;
    }
  }
  return 0;
}

int SocketEventQueue::SendToSelf(const void* data, uint32_t size)
{
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(Port);

  return SendTo(&addr, data, size);
}

int SocketEventQueue::SendTo(const sockaddr_in* addr, const void* data, uint32_t size)
{
  return sendto(ControlSocket, (char*)data, size, 0, (sockaddr*)addr, sizeof(sockaddr_in));
}
#endif

void SocketEventQueue::Stop()
{
  std::shared_ptr<std::jthread> thread;
  do
  {
    auto guard = DataLock.Lock();

    thread = Thread;
    Thread.reset();

  } while (false);

  if (thread)
  {
    SetEvent(EvStop);

#ifndef _WIN32
    int e = SendToSelf(CMD_EXIT, strlen(CMD_EXIT));
    if (e == -1)
    {
      LogosE("Failed to send @stop command");
    }
#endif

    thread.reset();
  }

  assert(Queue.empty());
}

#ifndef _WIN32
SocketEventQueue::ADD_EVENT_RESULT SocketEventQueue::Append(SocketEvent* socketEvent)
{
  auto guard = DataLock.Lock();

  if (WaitForSingleObject(EvStop, 0) == WAIT_RESULT::OBJECT_0)
    return ADD_EVENT_RESULT::FAILED;

  // One entry is used for ControlSocket
  size_t numEvents = Queue.size() + 1;
  size_t pollList = Events.size();

  if (numEvents + 1 <= pollList)
  {
    // epoll_wait(2) — Linux manual page:
    // While one thread is blocked in a call to epoll_wait(), it is
    // possible for another thread to add a file descriptor to the
    // waited-upon epoll instance.  If the new file descriptor becomes
    // ready, it will cause the epoll_wait() call to unblock

    epoll_event ev = socketEvent->GetPollEvent();

    if (ev.events == 0)
      return ADD_EVENT_RESULT::SUCCESS;

    ev.events |= EPOLLONESHOT | EPOLLET;
    if (epoll_ctl(Poll, EPOLL_CTL_ADD, socketEvent->Socket, &ev) == -1)
    {
      LogosE("epoll_ctl(EPOLL_CTL_ADD) failed");
      return ADD_EVENT_RESULT::FAILED;
    }

    Queue[socketEvent] = true;

    if (Thread == nullptr)
      Thread = std::make_shared<std::jthread>(&SocketEventQueue::Worker, this);

    return ADD_EVENT_RESULT::SUCCESS;
  }

  assert(Thread != nullptr);
  ResetEvent(EvGrowDone);

  int e = SendToSelf(CMD_GROW, strlen(CMD_GROW));
  if (e == -1)
  {
    LogosE("Failed to send @grow command");
    return ADD_EVENT_RESULT::FAILED;
  }

  return ADD_EVENT_RESULT::GROW;
}
#endif

bool SocketEventQueue::AddSocketEvent(SocketEvent* socketEvent)
{
#ifdef _WIN32
  return false;
#else
  for (;;)
  {
    ADD_EVENT_RESULT rc = Append(socketEvent);

    if (rc == ADD_EVENT_RESULT::FAILED)
      return false;

    if (rc == ADD_EVENT_RESULT::SUCCESS)
      return true;

    EventArray ev(EvStop, EvGrowDone);
    auto wr = WaitForMultipleObjects(ev, false, FOREVER);
    if (wr == WAIT_RESULT::OBJECT_0)
      break;

    assert(wr == WAIT_RESULT::OBJECT_1);
    if (wr != WAIT_RESULT::OBJECT_1)
      return false;
  }
  return false;
#endif  
}

bool SocketEventQueue::RemoveSocketEvent(SocketEvent* socketEvent)
{
#ifdef _WIN32
  return false;
#else
  auto guard = DataLock.Lock();
  if (!Queue.count(socketEvent))
    return false;

  Queue.erase(socketEvent);

  // In kernel versions before 2.6.9, the EPOLL_CTL_DEL operation
  // required a non-null pointer in event, even though this argument
  // is ignored.  Since Linux 2.6.9, event can be specified as NULL
  // when using EPOLL_CTL_DEL. Applications that need to be portable
  // to kernels before 2.6.9 should specify a non-null pointer in
  // event.
  int socket = socketEvent->Socket;
  epoll_event ev = socketEvent->GetPollEvent();

  if (epoll_ctl(Poll, EPOLL_CTL_DEL, socket, &ev) == -1)
  {
    LogosE("epoll_ctl(EPOLL_CTL_DEL) failed");
    return false;
  }
  return true;
#endif  
}

bool SocketEventQueue::Empty()
{
  auto guard = DataLock.Lock();
  return Queue.empty();
}

bool SocketEventQueue::ActivateEvent(SocketEvent* socketEvent)
{
#ifndef _WIN32
  auto guard = DataLock.Lock();
  if (!Queue.count(socketEvent))
    return false;

  // Do nothing if event is already activated
  if (Queue[socketEvent])
    return true;

  epoll_event ev = socketEvent->GetPollEvent();
  if (ev.events == 0)
    return false;

  ev.events |= EPOLLONESHOT | EPOLLET;
  if (epoll_ctl(Poll, EPOLL_CTL_MOD, socketEvent->Socket, &ev) == -1)
  {
    LogosE("epoll_ctl(EPOLL_CTL_MOD) failed");
    return false;
  }

  Queue[socketEvent] = true;
  return true;
#else
  return false;
#endif
}

#ifndef _WIN32
void SocketEventQueue::FireEvents(const epoll_event& e)
{
  auto guard = DataLock.Lock();

  SocketEvent* p = (SocketEvent*)e.data.ptr;
  if (p != nullptr)
  {
    // Ensure that RemoveSocketEvent was not called
    if (!Queue.count(p))
      return;

    // Check that event is activated
    assert(Queue[p] == true);

    // Mark event as removed from wait list
    Queue[p] = false;

    int events = 0;

    if (e.events & EPOLLIN)
      events |= EVENT_READ;

    if (e.events & EPOLLOUT)
      events |= EVENT_WRITE;

    if (e.events & EPOLLRDHUP)
      events |= EVENT_CLOSE;

    if (events)
      p->FireEvents(events);
  }
}

bool SocketEventQueue::ProcessEvents(int n)
{
  std::string command;
  for (int i = 0; i < n; i++)
  {
    auto& e = Events[i];
    if (e.data.fd == ControlSocket)
    {
      char buffer[512] = {'\0'};
      int cb = read(ControlSocket, buffer, sizeof(buffer));
      if (cb == -1)
      {
        LogosE("Failed to read control socket");
      }
      else if (cb)
        command = std::string(buffer, cb);

      continue;
    }

    FireEvents(e);
  }

  if (command == CMD_EXIT)
    return false;

  if (command == CMD_GROW)
  {
    do
    {
      auto guard = DataLock.Lock();

      size_t grow = size_t(OPTIONS::GROW_SIZE);
      size_t currentSize = Events.size();
      size_t newSize = (((currentSize + 1) / grow) + 1) * grow;

      Events.resize(newSize);
    } while (false);

    SetEvent(EvGrowDone);
    return true;
  }

  if (!command.empty())
  {
    LogmeW("Unsupported command: %s", command.c_str());
  }

  return true;
}
#endif

void SocketEventQueue::Worker()
{
#ifndef _WIN32
  SET_CUR_THREAD_NAME("SktEvQueueWorker");

  while (GetEventState(EvStop) != STATE::SIGNALLED)
  {
    int n = epoll_wait(Poll, &Events[0], int(Events.size()), -1);
    if (n < 0 && errno == EINTR)
      n = 0;

    if (n < 0)
    {
      LogosE("epoll_wait failed");
      break;
    }

    if (!ProcessEvents(n))
      break;
  }
#endif
}