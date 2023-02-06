#include <Syncme/Sockets/ErrorLimit.h>
#include <Syncme/TickCount.h>

using namespace Syncme;

ErrorLimit::ErrorLimit(size_t count, uint64_t duration)
  : Count(0)
  , Duration(0)
{
  SetLimit(count, duration);
}

void ErrorLimit::SetLimit(size_t count, uint64_t duration)
{
  Count = count;
  Duration = duration;
  History.clear();
}

void ErrorLimit::Clear()
{
  History.clear();
}

bool ErrorLimit::ReportError()
{
  uint64_t t = GetTimeInMillisec();
  for (bool cont = true; cont;)
  {
    cont = false;
    for (auto it = History.begin(); it != History.end(); ++it)
    {
      if (t - *it > Duration)
      {
        History.erase(it);
        cont = true;
        break;
      }
    }
  }

  History.push_back(t);
  return History.size() >= Count;
}
