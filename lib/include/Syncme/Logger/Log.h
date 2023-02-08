#pragma once

#ifdef USE_LOGME
  #include <Logme/Logme.h>

  #ifndef LogD
    #define LogD LogmeD
  #endif

  #ifndef LogI
    #define LogI LogmeI
  #endif

  #ifndef LogW
    #define LogW LogmeW
  #endif

  #ifndef LogE
    #define LogE LogmeE
  #endif

  #define LogosE(format) \
      {LogmeE(format ". Error: %s", OSERR2);}
#else
  #ifndef LogD
    #define LogD(...)
  #endif

  #ifndef LogI
    #define LogI(...)
  #endif

  #ifndef LogW
    #define LogW(...)
  #endif

  #ifndef LogE
    #define LogE(...)
  #endif

  #define LogosE(format)
#endif
