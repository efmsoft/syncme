#include <Syncme/Sockets/TlsHello.h>

#if defined(USE_BORINGSSL)

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

namespace
{
  struct HelloCtx
  {
    bool GotHello = false;
    Syncme::ClientHelloInfo* Out = nullptr;
    std::vector<std::vector<uint8_t>>* Packets = nullptr;
  };

  static void MsgCallback(
    int write_p
    , int /*version*/
    , int /*content_type*/
    , const void* buf
    , size_t len
    , SSL* /*ssl*/
    , void* arg
  )
  {
    HelloCtx* ctx = reinterpret_cast<HelloCtx*>(arg);
    if(!ctx || ctx->GotHello || !ctx->Packets || !buf || len == 0)
    {
      return;
    }

    // Capture only inbound (client->server) raw bytes before ClientHello is processed.
    if(write_p)
    {
      return;
    }

    const uint8_t* p = reinterpret_cast<const uint8_t*>(buf);
    ctx->Packets->emplace_back(p, p + len);
  }

  static ssl_select_cert_result_t SelectCertCallback(
    const SSL_CLIENT_HELLO* client_hello
    , void* arg
  )
  {
    HelloCtx* ctx = reinterpret_cast<HelloCtx*>(arg);
    if(!ctx || !ctx->Out)
    {
      return ssl_select_cert_error;
    }

    // SNI
    const uint8_t* ext_data = nullptr;
    size_t ext_len = 0;
    if(SSL_early_callback_ctx_extension_get(
      client_hello
      , TLSEXT_TYPE_server_name
      , &ext_data
      , &ext_len
    ))
    {
      // ServerNameList is: 2 bytes list_len, then entries:
      // 1 byte name_type, 2 bytes name_len, name bytes.
      if(ext_len >= 5)
      {
        size_t pos = 0;
        size_t list_len = (static_cast<size_t>(ext_data[pos]) << 8) | ext_data[pos + 1];
        pos += 2;

        if(list_len + 2 <= ext_len)
        {
          if(pos + 3 <= ext_len && ext_data[pos] == 0x00) // host_name
          {
            pos += 1;
            size_t name_len = (static_cast<size_t>(ext_data[pos]) << 8) | ext_data[pos + 1];
            pos += 2;

            if(pos + name_len <= ext_len)
            {
              ctx->Out->Sni.assign(reinterpret_cast<const char*>(ext_data + pos), name_len);
            }
          }
        }
      }
    }

    // ALPN
    ext_data = nullptr;
    ext_len = 0;
    if(SSL_early_callback_ctx_extension_get(
      client_hello
      , TLSEXT_TYPE_application_layer_protocol_negotiation
      , &ext_data
      , &ext_len
    ))
    {
      // ALPN extension data is: 2 bytes list_len, then entries:
      // 1 byte proto_len, proto bytes.
      if(ext_len >= 2)
      {
        size_t pos = 0;
        size_t list_len = (static_cast<size_t>(ext_data[pos]) << 8) | ext_data[pos + 1];
        pos += 2;

        if(list_len + 2 <= ext_len)
        {
          while(pos < ext_len)
          {
            uint8_t proto_len = ext_data[pos++];
            if(proto_len == 0 || pos + proto_len > ext_len)
            {
              break;
            }

            ctx->Out->Alpn.emplace_back(
              reinterpret_cast<const char*>(ext_data + pos)
              , static_cast<size_t>(proto_len)
            );
            pos += proto_len;
          }
        }
      }
    }

    ctx->GotHello = true;

    // We only need ClientHello. Abort handshake intentionally.
    return ssl_select_cert_error;
  }

  static std::string GetOpenSslErrorString()
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

namespace Syncme
{
  static ClientHelloStatus PeekClientHelloImpl(
    const uint8_t* data
    , size_t size
    , ClientHelloInfo& out
    , std::vector<std::vector<uint8_t>>* packets
    , std::string& error
  )
  {
    error.clear();
    out.Sni.clear();
    out.Alpn.clear();
    if(packets)
    {
      packets->clear();
    }

    if(!data || size == 0)
    {
      return ClientHelloStatus::NEED_MORE;
    }

    SSL_CTX* ctx = SSL_CTX_new(TLS_method());
    if(!ctx)
    {
      error = "SSL_CTX_new failed: " + GetOpenSslErrorString();
      return ClientHelloStatus::ERROR;
    }

    HelloCtx hello;
    hello.GotHello = false;
    hello.Out = &out;
    hello.Packets = packets;

    SSL_CTX_set_select_certificate_cb(ctx, SelectCertCallback, &hello);
    SSL_CTX_set_msg_callback(ctx, MsgCallback);
    SSL_CTX_set_msg_callback_arg(ctx, &hello);

    SSL* ssl = SSL_new(ctx);
    if(!ssl)
    {
      error = "SSL_new failed: " + GetOpenSslErrorString();
      SSL_CTX_free(ctx);
      return ClientHelloStatus::ERROR;
    }

    BIO* rbio = BIO_new(BIO_s_mem());
    BIO* wbio = BIO_new(BIO_s_mem());
    if(!rbio || !wbio)
    {
      error = "BIO_new failed: " + GetOpenSslErrorString();
      if(rbio) BIO_free(rbio);
      if(wbio) BIO_free(wbio);
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      return ClientHelloStatus::ERROR;
    }

    BIO_write(rbio, data, static_cast<int>(size));
    SSL_set_bio(ssl, rbio, wbio);
    SSL_set_accept_state(ssl);

    int ret = SSL_do_handshake(ssl);
    if(hello.GotHello)
    {
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      return ClientHelloStatus::OK;
    }

    if(ret == 1)
    {
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      return ClientHelloStatus::OK;
    }

    int err = SSL_get_error(ssl, ret);
    if(err == SSL_ERROR_WANT_READ)
    {
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      return ClientHelloStatus::NEED_MORE;
    }

    error = "SSL_do_handshake failed: " + GetOpenSslErrorString();
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    return ClientHelloStatus::ERROR;
  }

  ClientHelloStatus PeekClientHello(
    const uint8_t* data
    , size_t size
    , ClientHelloInfo& out
    , std::string& error
  )
  {
    return PeekClientHelloImpl(data, size, out, nullptr, error);
  }

  ClientHelloStatus PeekClientHelloWithPackets(
    const uint8_t* data
    , size_t size
    , ClientHelloInfo& out
    , std::vector<std::vector<uint8_t>>& packets
    , std::string& error
  )
  {
    return PeekClientHelloImpl(data, size, out, &packets, error);
  }
}

#else

namespace Syncme
{
  ClientHelloStatus PeekClientHello(
    const uint8_t* /*data*/
    , size_t /*size*/
    , ClientHelloInfo& /*out*/
    , std::string& error
  )
  {
    error = "PeekClientHello is only available when USE_BORINGSSL is enabled";
    return ClientHelloStatus::ERROR;
  }
}

#endif
