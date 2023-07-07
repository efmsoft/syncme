#include <Syncme/Uninitialize.h>

using namespace Syncme::Implementation;

static UninitializeEntry* FirstEntry;

void Syncme::Implementation::CallOnUninitialize(UninitializeEntry* entry)
{
  entry->Next = FirstEntry;
  FirstEntry = entry;
}

void Syncme::Uninitialize()
{
  for (UninitializeEntry* p = FirstEntry; p; p = p->Next)
    p->Callback();
}