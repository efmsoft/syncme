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

#ifdef SYNCME_BUILD
  #ifdef USE_LOGME
    #define LogD LogmeD
    #define LogI LogmeI
    #define LogW LogmeW
    #define LogE LogmeE

    #define LogEwsa(format) \
        {LogmeE(format ". Error: %s", OSERR2);}
  #else
    #define LogD(...)
    #define LogI(...)
    #define LogW(...)
    #define LogE(...)

    #define LogEwsa(format)
  #endif
#endif