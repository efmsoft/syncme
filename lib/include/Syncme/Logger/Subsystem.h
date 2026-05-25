#pragma once

#ifdef USE_LOGME
#include <Logme/Logme.h>

namespace Syncme
{
  typedef Logme::SID SUBSYSTEM;
}
#else
namespace Syncme
{
  typedef int SUBSYSTEM;
}
#endif
