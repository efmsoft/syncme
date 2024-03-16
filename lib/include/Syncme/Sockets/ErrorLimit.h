#pragma once

#include <list>
#include <memory>
#include <stdint.h>

#include <Syncme/Api.h>

namespace Syncme
{
  class ErrorLimit
  {
    size_t Count;
    uint64_t Duration;
    std::list<uint64_t> History;

  public:
    SINCMELNK ErrorLimit(size_t count, uint64_t duration);

    SINCMELNK void SetLimit(size_t count, uint64_t duration);
    SINCMELNK void Clear();

    SINCMELNK bool ReportError();
  };

  typedef std::shared_ptr<ErrorLimit> ErrorLimitPtr;
}