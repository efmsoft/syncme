#pragma once

#include <chrono>
#include <stdint.h>

namespace Syncme
{
  typedef std::chrono::high_resolution_clock Clock;
  typedef std::chrono::time_point<Clock> TimeValue;

  class TimePoint;
  int64_t operator-(const TimePoint& t1, const TimePoint& t2);

  class TimePoint
  {
    TimeValue Value;

  public:
    TimePoint(const TimeValue& v = Clock::now())
      : Value(v)
    {
    }

    int64_t ElapsedSince() const
    {
      TimePoint t2;
      return t2 - *this;
    }

    const TimeValue& Get() const
    {
      return Value;
    }
  };

  inline int64_t operator-(const TimePoint& t1, const TimePoint& t2)
  {
    auto d = std::chrono::duration_cast<std::chrono::milliseconds>(t1.Get() - t2.Get());
    return d.count();
  }
}