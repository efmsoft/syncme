#pragma once

#include <list>
#include <memory>
#include <stdint.h>

namespace Syncme
{
  class ErrorLimit
  {
    size_t Count;
    uint64_t Duration;
    std::list<uint64_t> History;

  public:
    ErrorLimit(size_t count, uint64_t duration);

    void SetLimit(size_t count, uint64_t duration);
    void Clear();

    bool ReportError();
  };

  typedef std::shared_ptr<ErrorLimit> ErrorLimitPtr;
}