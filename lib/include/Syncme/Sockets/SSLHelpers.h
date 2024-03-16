#pragma once

#include <string>

#include <Syncme/Api.h>

namespace Syncme
{
  SINCMELNK std::string SSLProtocolName(int version);
  SINCMELNK std::string SSLContentType(int content_type);
  SINCMELNK std::string SSLPacketDescr(
    int version
    , int content_type
    , const void* buf
    , size_t len
  );
  SINCMELNK std::string TlsExtType(int type);
  SINCMELNK std::string SecurityCallbackType(int type);
  SINCMELNK std::string Tls13Scheme(int type);
  SINCMELNK std::string Tls12Alg(int type);
  SINCMELNK std::string Tls12Hash(int type);
  SINCMELNK std::string SslError(int code);
  SINCMELNK std::string GetBioError();
}