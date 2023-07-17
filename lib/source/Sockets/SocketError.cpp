#include <cassert>

#include <Syncme/Sockets/SocketError.h>

using namespace Syncme;

SocketError::SocketError()
{
  SET_SOCKET_ERROR(*this, NONE);
}

SocketError::SocketError(SKT_ERROR code, const char* file, int line)
{
  Set(code, file, line);
}

void SocketError::Set(SKT_ERROR code, const char* file, int line)
{
  Code = code;
  File = file;
  Line = line;
}

SocketError::operator SKT_ERROR() const
{
  return Code;
}

std::string SocketError::ToString(SKT_ERROR c)
{
  switch (c)
  {
  case SKT_ERROR::NONE: return "NONE";
  case SKT_ERROR::TIMEOUT: return "TIMEOUT";
  case SKT_ERROR::GRACEFUL_DISCONNECT: return "GRACEFUL_DISCONNECT";
  case SKT_ERROR::WOULDBLOCK: return "WOULDBLOCK";
  case SKT_ERROR::IO_INCOMPLETE: return "IO_INCOMPLETE";
  case SKT_ERROR::CONNECTION_ABORTED: return "CONNECTION_ABORTED";
  case SKT_ERROR::GENERIC: return "GENERIC";
  default:
    break;
  }

  assert(!"unsupported error code");
  return std::string();
}

std::string SocketError::Format() const
{
  std::string str(ToString(Code));

#ifdef _WIN32
  const char* p = strrchr(File, '\\');
#else
  const char* p = strrchr(File, '/');
#endif

  if (p == nullptr)
    return str;

  str += " (";
  str += ++p;
  return str + ", " + std::to_string(Line) + ")";
}
