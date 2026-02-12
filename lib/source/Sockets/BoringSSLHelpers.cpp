#if defined(USE_BORINGSSL)

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <Syncme/Sockets/SSLHelpers.h>

namespace Syncme
{
  std::string SSLProtocolName(int version)
  {
    switch(version)
    {
      case TLS1_VERSION: return "TLS1.0";
      case TLS1_1_VERSION: return "TLS1.1";
      case TLS1_2_VERSION: return "TLS1.2";
      case TLS1_3_VERSION: return "TLS1.3";
      default: break;
    }
    return std::to_string(version);
  }

  std::string SSLContentType(int content_type)
  {
    switch(content_type)
    {
      case SSL3_RT_CHANGE_CIPHER_SPEC: return "change_cipher_spec";
      case SSL3_RT_ALERT: return "alert";
      case SSL3_RT_HANDSHAKE: return "handshake";
      case SSL3_RT_APPLICATION_DATA: return "application_data";
      default: break;
    }
    return std::to_string(content_type);
  }

  std::string SSLPacketDescr(
    int version
    , int content_type
    , const void* /*buf*/
    , size_t /*len*/
  )
  {
    // BoringSSL build: keep this as a lightweight helper.
    // Detailed parsing is OpenSSL-specific in the current codebase.
    return SSLProtocolName(version) + " " + SSLContentType(content_type);
  }

  std::string TlsExtType(int type)
  {
    return std::to_string(type);
  }

  std::string SecurityCallbackType(int type)
  {
    return std::to_string(type);
  }

  std::string Tls13Scheme(int type)
  {
    return std::to_string(type);
  }

  std::string Tls12Alg(int type)
  {
    return std::to_string(type);
  }

  std::string Tls12Hash(int type)
  {
    return std::to_string(type);
  }

  std::string SslError(int code)
  {
    if(code == 0)
    {
      return "OK";
    }

    char buf[256];
    buf[0] = 0;
    ERR_error_string_n(static_cast<unsigned long>(code), buf, sizeof(buf));
    return std::string(buf);
  }

  std::string GetBioError()
  {
    unsigned long code = ERR_get_error();
    if(code == 0)
    {
      return std::string();
    }

    char buf[256];
    buf[0] = 0;
    ERR_error_string_n(code, buf, sizeof(buf));
    return std::string(buf);
  }
}

#endif
