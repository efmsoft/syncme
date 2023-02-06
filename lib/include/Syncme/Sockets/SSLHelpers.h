#pragma once

#include <string>

namespace Syncme
{
  std::string SSLProtocolName(int version);
  std::string SSLContentType(int content_type);
  std::string SSLPacketDescr(
    int version
    , int content_type
    , const void* buf
    , size_t len
  );
  std::string TlsExtType(int type);
  std::string SecurityCallbackType(int type);
  std::string Tls13Scheme(int type);
  std::string Tls12Alg(int type);
  std::string Tls12Hash(int type);
  std::string SslError(int code);
  std::string GetBioError();
}