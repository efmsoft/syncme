#pragma once

#include <string>

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
    SocketError();
    SocketError(SKT_ERROR code, const char* file, int line);

    void Set(SKT_ERROR code, const char* file, int line);
    operator SKT_ERROR () const;

    static std::string ToString(SKT_ERROR c);
    std::string Format() const;
  };

  #define SET_SOCKET_ERROR(e, c) \
    (e).Set(SKT_ERROR::c, __FILE__, __LINE__)
}
