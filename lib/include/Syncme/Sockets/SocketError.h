#pragma once

#include <string>

#include <Syncme/Api.h>

namespace Syncme
{
  enum class SKT_ERROR
  {
    NONE,

    TIMEOUT,
    GRACEFUL_DISCONNECT,
    WOULDBLOCK,
    IO_INCOMPLETE,
    CONNECTION_ABORTED,
    GENERIC,
  };

  struct SocketError
  {
    SKT_ERROR Code;
    const char* File;
    int Line;

  public:
    SINCMELNK SocketError();
    SINCMELNK SocketError(SKT_ERROR code, const char* file, int line);

    SINCMELNK void Set(SKT_ERROR code, const char* file, int line);
    SINCMELNK operator SKT_ERROR () const;

    SINCMELNK static std::string ToString(SKT_ERROR c);
    SINCMELNK std::string Format() const;
  };

  #define SET_SOCKET_ERROR(e, c) \
    (e).Set(SKT_ERROR::c, __FILE__, __LINE__)
}
