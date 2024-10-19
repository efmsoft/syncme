#include <Syncme/TaskQueue.h>

using namespace Syncme;
using namespace Syncme::Task;

Item::Item(ItemCallback p, void* context, const char* identifier)
  : Identifier(identifier)
  , Callback(p)
  , Context(context)
  , Completed(CreateNotificationEvent())
{
}

Item::Item(TCallback functor, const char* identifier)
  : Identifier(identifier)
  , Callback(&Item::DefaultCallback)
  , Context(this)
  , Functor(functor)
  , Completed(CreateNotificationEvent())
{
}

Item::~Item()
{
  WaitForCompletion();
}

void Item::Invoke()
{
  Callback(Context);
  SetEvent(Completed);
}

void Item::Cancel()
{
  SetEvent(Completed);
}

void Item::WaitForCompletion() const
{
  WaitForSingleObject(Completed);
}

void Item::DefaultCallback(void* context)
{
  Item* self = (Item*)context;
  self->Functor();
}


