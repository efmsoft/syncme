#include <cassert>

#include <Syncme/Sync.h>

using namespace Syncme;

EventArray::EventArray(
  HEvent e1
  , HEvent e2
  , HEvent e3
  , HEvent e4
  , HEvent e5
  , HEvent e6
  , HEvent e7
  , HEvent e8
)
{
  if (e1)
    push_back(e1);

  if (e2)
  {
    assert(e1);
    push_back(e2);
  }

  if (e3)
  {
    assert(e1 && e2);
    push_back(e3);
  }

  if (e4)
  {
    assert(e1 && e2 && e3);
    push_back(e4);
  }

  if (e5)
  {
    assert(e1 && e2 && e3 && e4);
    push_back(e5);
  }

  if (e6)
  {
    assert(e1 && e2 && e3 && e4 && e5);
    push_back(e6);
  }

  if (e7)
  {
    assert(e1 && e2 && e3 && e4 && e5 && e6);
    push_back(e7);
  }

  if (e8)
  {
    assert(e1 && e2 && e3 && e4 && e5 && e6 && e7);
    push_back(e8);
  }
}
