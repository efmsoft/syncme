#pragma once

#include <Syncme/Api.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Syncme
{
  enum class ClientHelloStatus
  {
    NEED_MORE = 0,
    OK        = 1,
    ERROR     = 2,
  };

  struct ClientHelloInfo
  {
    std::string Sni;
    std::vector<std::string> Alpn;
  };

  // Parses ClientHello from the provided raw TLS bytes.
  // Returns:
  // - NEED_MORE: not enough bytes yet, feed more data and try again.
  // - OK: out contains extracted SNI/ALPN.
  // - ERROR: parsing failed.
  SINCMELNK ClientHelloStatus PeekClientHello(
    const uint8_t* data
    , size_t size
    , ClientHelloInfo& out
    , std::string& error
  );

  // Same as PeekClientHello, but additionally captures raw client-side TLS bytes
  // observed before ClientHello callback is reached. This is used by SSLBYPASS.
  SINCMELNK ClientHelloStatus PeekClientHelloWithPackets(
    const uint8_t* data
    , size_t size
    , ClientHelloInfo& out
    , std::vector<std::vector<uint8_t>>& packets
    , std::string& error
  );
}
