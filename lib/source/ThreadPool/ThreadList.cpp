#include <cassert>

#include <Syncme/ThreadPool/ThreadList.h>

using namespace Syncme;

ThreadsList::ThreadsList()
{
}

ThreadsList::~ThreadsList()
{
  Wait();
}

void ThreadsList::Add(HEvent h)
{
  List.push_back(h);
  Update();
}

size_t ThreadsList::Update()
{
  for (bool cont = true; cont;)
  {
    cont = false;
    for (auto it = List.begin(); it != List.end(); ++it)
    {
      auto rc = WaitForSingleObject(*it, 0);
      if (rc == WAIT_RESULT::OBJECT_0)
      {
        CloseHandle(*it);
        List.erase(it);

        cont = true;
        break;
      }

      assert(rc == WAIT_RESULT::TIMEOUT);
    }
  }

  return List.size();
}

void ThreadsList::Wait()
{
  while (!List.empty())
  {
    auto it = List.begin();
    auto rc = WaitForSingleObject(*it, FOREVER);

    if (rc == WAIT_RESULT::OBJECT_0)
    {
      CloseHandle(*it);
      List.erase(it);
      continue;
    }

    assert(!"Invalid handle?!?");
  }
}
