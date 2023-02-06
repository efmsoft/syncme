#pragma once

#ifdef USE_LOGME
  #include <Logme/Logme.h>

  #define LogD LogmeD
  #define LogI LogmeI
  #define LogW LogmeW
  #define LogE LogmeE

  #define LogEwsa(format) \
    {LogmeE(format ". Error: %s", OSERR2);}

  namespace Syncme
  {
    typedef Logme::ID CHANNEL;
  }
#else
  #define LogD(...)
  #define LogI(...)
  #define LogW(...)
  #define LogE(...)

  #define LogEwsa(format)

  namespace Syncme
  {
    typedef int CHANNEL;
  }
#endif