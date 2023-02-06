#include <Syncme/TickCount.h>
#include <Syncme/Timer/Timer.h>

using namespace Syncme::Implementation;

Timer::Timer(HEvent timer, long period, std::function<void(HEvent)> callback)
  : EvTimer(timer)
  , Period(period)
  , Callback(callback)
  , NextDueTime{}
{
}

Timer::~Timer()
{
}

void Timer::Set(long dueTime)
{
  NextDueTime = GetTimeInMillisec() + Period;
}
