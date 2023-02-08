#pragma once

#ifdef USE_LOGME
#include <Logme/Logme.h>

namespace Syncme
{
  typedef Logme::ID CHANNEL;
}
#else
namespace Syncme
{
  typedef int CHANNEL;
}
#endif
