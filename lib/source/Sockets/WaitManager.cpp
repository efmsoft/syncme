#include <cassert>
#include <list>

#include <Syncme/CritSection.h>
#include <Syncme/Sockets/WaitManager.h>
#include <Syncme/Sockets/WaitThread.h>
#include <Syncme/Uninitialize.h>

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
