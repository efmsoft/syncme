#pragma once

#ifdef USE_LOGME
  #include <Logme/Logme.h>

  #define LogD LogmeD
  #define LogI LogmeI
  #define LogW LogmeW
  #define LogE LogmeE

  #define LogosE(format) \
      {LogmeE(format ". Error: %s", OSERR2);}
#else
  #define LogD(...)
  #define LogI(...)
  #define LogW(...)
  #define LogE(...)

  #define LogosE(format)
#endif
