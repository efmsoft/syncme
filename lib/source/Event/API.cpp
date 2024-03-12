#include <cassert>

#include <Syncme/Event/Event.h>
#include <Syncme/Sync.h>

using namespace Syncme;

HEvent Syncme::CreateNotificationEvent(STATE state)
{
  return std::shared_ptr<Syncme::Event>(
    new Syncme::Event(true, state != STATE::NOT_SIGNALLED)
    , Syncme::EventDeleter()
  );
}

HEvent Syncme::CreateSynchronizationEvent(STATE state)
{
  return std::shared_ptr<Syncme::Event>(
    new Syncme::Event(false, state != STATE::NOT_SIGNALLED)
    , Syncme::EventDeleter()
  );
}

bool Syncme::CloseHandle(HEvent& event)
{
  if (event == nullptr)
    return false;
  
  event->OnCloseHandle();
  event.reset();

  return true;
}

HEvent Syncme::DuplicateHandle(HEvent event)
{
  HEvent e = std::shared_ptr<Syncme::Event>(
    new Syncme::Event(event->Notification, event->Signalled)
    , Syncme::EventDeleter()
  );

  if (e != nullptr)
    e->BindTo(event.get());
  
  return e;
}

STATE Syncme::GetEventState(HEvent event)
{
  if (event == nullptr)
    return STATE::UNDEFINED;

  return event->IsSignalled() ? STATE::SIGNALLED : STATE::NOT_SIGNALLED;
}

bool Syncme::SetEvent(HEvent event)
{
  if (event == nullptr)
    return false;

  event->SetEvent(event.get());
  return true;
}

bool Syncme::ResetEvent(HEvent event)
{
  if (event == nullptr)
    return false;
  
  event->ResetEvent(event.get());
  return true;
}
