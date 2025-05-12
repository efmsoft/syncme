#include <cassert>

#include <Syncme/Logger/Log.h>
#include <Syncme/SetThreadName.h>
#include <Syncme/Sockets/WaitThread.h>
#include <Syncme/Sockets/SocketEvent.h>

#ifdef _WIN32
#include <Windows.h>

using namespace Syncme::Implementation;

static DWORD WINAPI ThreadStartup(void* p)
{
  WaitThread* object = (WaitThread*)p;
  object->Worker();
  return 0;
}

WaitThread::WaitThread()
  : EmptySince(0)
  , ID(0)
  , Thread(nullptr)
  , EvExit(nullptr)
  , EvRestart(nullptr)
  , EvDone(nullptr)
{
  EvExit = CreateEventA(nullptr, true, false, nullptr);
  if (EvExit == nullptr)
    LogosE("Unable to create EvExit");

  EvRestart = CreateEventA(nullptr, false, false, nullptr);
  if (EvRestart == nullptr)
    LogosE("Unable to create EvRestart");

  EvDone = CreateEventA(nullptr, false, false, nullptr);
  if (EvDone == nullptr)
    LogosE("Unable to create EvDone");
}

WaitThread::~WaitThread()
{
  Stop();

  if (EvDone)
    ::CloseHandle(EvDone);

  if (EvRestart)
    ::CloseHandle(EvRestart);

  if (EvExit)
    ::CloseHandle(EvExit);
}

bool WaitThread::Run()
{
  if (!EvDone || !EvRestart || !EvExit)
    return false;

  if (Thread)
    return true;

  Thread = CreateThread(nullptr, 0, &ThreadStartup, this, 0, &ID);
  if (Thread == nullptr)
    LogosE("CreateThread failed");

  return Thread != nullptr;
}

void WaitThread::Stop()
{
  if (Thread)
  {
    ::SetEvent(EvExit);
    ::WaitForSingleObject(Thread, INFINITE);
    ::CloseHandle(Thread);

    Thread = nullptr;
  }
}

bool WaitThread::Empty()
{
  auto lock = DataLock.Lock();
  return Events.empty();
}

unsigned WaitThread::TicksSinceEmpty()
{
  auto lock = DataLock.Lock();

  if (Events.empty() == false)
    return 0;

  return unsigned(GetTickCount64() - EmptySince);
}

bool WaitThread::Add(SocketEvent* object)
{
  if (true)
  {
    auto lock = DataLock.Lock();

    for (auto& e : Events)
    {
      if (object == e)
        return true;
    }

    // We can not wait more than MAXIMUM_WAIT_OBJECTS objects
    if (Events.size() + 2 >= MAXIMUM_WAIT_OBJECTS)
      return false;

    Events.push_back(object);
  }

  Restart();
  return true;
}

bool WaitThread::Remove(SocketEvent* object)
{
  if (RemoveInternal(object))
  {
    Restart();
    return true;
  }
  return false;
}

void WaitThread::Restart()
{
  ::ResetEvent(EvDone);
  ::SetEvent(EvRestart);
  ::WaitForSingleObject(EvDone, INFINITE);
}

bool WaitThread::RemoveInternal(SocketEvent* object)
{
  auto lock = DataLock.Lock();

  for (auto it = Events.begin(); it != Events.end(); ++it)
  {
    SocketEvent* e = *it;

    if (object == e)
    {
      Events.erase(it);
      
      if (Events.empty())
        EmptySince = GetTickCount64();

      return true;
    }
  }

  return false;
}

void WaitThread::CreateWaitList(std::vector<void*>& object)
{
  auto lock = DataLock.Lock();
  object.push_back(EvExit);
  object.push_back(EvRestart);

  for (auto& e : Events)
    object.push_back(e->WSAEvent);
}

void WaitThread::TriggerEvent(HANDLE h)
{
  auto lock = DataLock.Lock();

  for (auto it = Events.begin(); it != Events.end(); ++it)
  {
    SocketEvent* e = *it;
    if (e->WSAEvent != h)
      continue;

    Events.erase(it);
    e->SetEvent(e);
    return;
  }
}

void WaitThread::Worker()
{
  SET_CUR_THREAD_NAME("WaitThread");

  while (true)
  {
    std::vector<HANDLE> object;
    CreateWaitList(object);

    auto rc = ::WaitForMultipleObjects(DWORD(object.size()), &object[0], false, INFINITE);
    assert(rc != WAIT_FAILED);
    if (rc == WAIT_FAILED)
      break;

    if (rc == WAIT_OBJECT_0)
      break;

    if (rc == WAIT_OBJECT_0 + 1)
    {
      ::SetEvent(EvDone);
      continue;
    }

    TriggerEvent(object[rc]);
  }

  ::SetEvent(EvDone);
}

#endif