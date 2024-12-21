#include <cassert>
#include <list>

#include <Syncme/CritSection.h>
#include <Syncme/Sockets/SocketEventQueue.h>
#include <Syncme/Sockets/WaitManager.h>
#include <Syncme/Sockets/WaitThread.h>
#include <Syncme/Uninitialize.h>

#ifdef _WIN32

using namespace Syncme;
using namespace Syncme::Implementation;

static CS DataLock;
static std::list<WaitThreadPtr> Threads;

ON_SYNCME_UNINITIALIZE(&Syncme::Implementation::WaitManager::Uninitialize)

void Syncme::Implementation::WaitManager::Uninitialize()
{
  auto lock = DataLock.Lock();
  
  for (auto& t : Threads)
    t->Stop();

  Threads.clear();
}

void Syncme::Implementation::WaitManager::AddSocketEvent(SocketEvent* e)
{
  auto lock = DataLock.Lock();

  for (auto& t : Threads)
  {
    if (t->Add(e))
      return;
  }

  WaitThreadPtr t = std::make_shared<WaitThread>();
  if (!t->Run())
    return;

  if (t->Add(e))
    Threads.push_back(t);
}

void Syncme::Implementation::WaitManager::RemoveSocketEvent(SocketEvent* e)
{
  auto lock = DataLock.Lock();

  for (auto it = Threads.begin(); it != Threads.end(); ++it)
  {
    auto& t = *it;

    if (!t->Remove(e))
      continue;

    if (t->Empty())
    {
      t->Stop();
      Threads.erase(it);
    }

    break;
  }
}
#else

#include <poll.h>

void Syncme::Implementation::WaitManager::AddSocketEvent(SocketEvent* e)
{
  struct pollfd pfd {};
  pfd.fd = e->Socket;
  pfd.revents = 0;
  pfd.events = 0;

  if (e->EventMask & EVENT_READ)
    pfd.events |= POLLIN;

  if (e->EventMask & EVENT_WRITE)
    pfd.events |= POLLOUT;

  if (e->EventMask & EVENT_CLOSE)
    pfd.events |= POLLHUP;

  struct timeval tv {};
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  int rc = poll(&pfd, 1, 0);
  if (rc > 0)
  {
    int events = 0;

    if (pfd.revents & POLLIN)
      events |= EVENT_READ;

    if (pfd.revents & POLLOUT)
      events |= EVENT_WRITE;

    if (pfd.revents & POLLHUP)
      events |= EVENT_CLOSE;

    if (events)
    {
      e->FireEvents(events);
      return;
    }

    if (e->Closed)
      return;
  }

  auto guard = SocketEventQueue::RemoveLock.Lock();
  auto& queue = SocketEventQueue::Ptr();

  if (queue)
    queue->ActivateEvent(e);
}

void Syncme::Implementation::WaitManager::RemoveSocketEvent(SocketEvent* e)
{
}
#endif
