#include <openssl/err.h>
#include <openssl/ssl2.h>
#include <openssl/ssl3.h>
#include <openssl/prov_ssl.h>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/SSLHelpers.h>

struct STRINT_PAIR
{
  const char* String;
  int Value;
};

static STRINT_PAIR alert_types[] = {
  {" close_notify", 0},
  {" end_of_early_data", 1},
  {" unexpected_message", 10},
  {" bad_record_mac", 20},
  {" decryption_failed", 21},
  {" record_overflow", 22},
  {" decompression_failure", 30},
  {" handshake_failure", 40},
  {" bad_certificate", 42},
  {" unsupported_certificate", 43},
  {" certificate_revoked", 44},
  {" certificate_expired", 45},
  {" certificate_unknown", 46},
  {" illegal_parameter", 47},
  {" unknown_ca", 48},
  {" access_denied", 49},
  {" decode_error", 50},
  {" decrypt_error", 51},
  {" export_restriction", 60},
  {" protocol_version", 70},
  {" insufficient_security", 71},
  {" internal_error", 80},
  {" inappropriate_fallback", 86},
  {" user_canceled", 90},
  {" no_renegotiation", 100},
  {" missing_extension", 109},
  {" unsupported_extension", 110},
  {" certificate_unobtainable", 111},
  {" unrecognized_name", 112},
  {" bad_certificate_status_response", 113},
  {" bad_certificate_hash_value", 114},
  {" unknown_psk_identity", 115},
  {" certificate_required", 116},
  {NULL}
};

static STRINT_PAIR handshakes[] = {
  {", HelloRequest", SSL3_MT_HELLO_REQUEST},
  {", ClientHello", SSL3_MT_CLIENT_HELLO},
  {", ServerHello", SSL3_MT_SERVER_HELLO},
  {", HelloVerifyRequest", DTLS1_MT_HELLO_VERIFY_REQUEST},
  {", NewSessionTicket", SSL3_MT_NEWSESSION_TICKET},
  {", EndOfEarlyData", SSL3_MT_END_OF_EARLY_DATA},
  {", EncryptedExtensions", SSL3_MT_ENCRYPTED_EXTENSIONS},
  {", Certificate", SSL3_MT_CERTIFICATE},
  {", ServerKeyExchange", SSL3_MT_SERVER_KEY_EXCHANGE},
  {", CertificateRequest", SSL3_MT_CERTIFICATE_REQUEST},
  {", ServerHelloDone", SSL3_MT_SERVER_DONE},
  {", CertificateVerify", SSL3_MT_CERTIFICATE_VERIFY},
  {", ClientKeyExchange", SSL3_MT_CLIENT_KEY_EXCHANGE},
  {", Finished", SSL3_MT_FINISHED},
  {", CertificateUrl", SSL3_MT_CERTIFICATE_URL},
  {", CertificateStatus", SSL3_MT_CERTIFICATE_STATUS},
  {", SupplementalData", SSL3_MT_SUPPLEMENTAL_DATA},
  {", KeyUpdate", SSL3_MT_KEY_UPDATE},
#ifndef OPENSSL_NO_NEXTPROTONEG
  {", NextProto", SSL3_MT_NEXT_PROTO},
#endif
  {", MessageHash", SSL3_MT_MESSAGE_HASH},
  {NULL}
};

static STRINT_PAIR tlsext_types[] = {
  {"server name", TLSEXT_TYPE_server_name},
  {"max fragment length", TLSEXT_TYPE_max_fragment_length},
  {"client certificate URL", TLSEXT_TYPE_client_certificate_url},
  {"trusted CA keys", TLSEXT_TYPE_trusted_ca_keys},
  {"truncated HMAC", TLSEXT_TYPE_truncated_hmac},
  {"status request", TLSEXT_TYPE_status_request},
  {"user mapping", TLSEXT_TYPE_user_mapping},
  {"client authz", TLSEXT_TYPE_client_authz},
  {"server authz", TLSEXT_TYPE_server_authz},
  {"cert type", TLSEXT_TYPE_cert_type},
  {"supported_groups", TLSEXT_TYPE_supported_groups},
  {"EC point formats", TLSEXT_TYPE_ec_point_formats},
  {"SRP", TLSEXT_TYPE_srp},
  {"signature algorithms", TLSEXT_TYPE_signature_algorithms},
  {"use SRTP", TLSEXT_TYPE_use_srtp},
  {"session ticket", TLSEXT_TYPE_session_ticket},
  {"renegotiation info", TLSEXT_TYPE_renegotiate},
  {"signed certificate timestamps", TLSEXT_TYPE_signed_certificate_timestamp},
  {"TLS padding", TLSEXT_TYPE_padding},
#ifdef TLSEXT_TYPE_next_proto_neg
  {"next protocol", TLSEXT_TYPE_next_proto_neg},
#endif
#ifdef TLSEXT_TYPE_encrypt_then_mac
  {"encrypt-then-mac", TLSEXT_TYPE_encrypt_then_mac},
#endif
#ifdef TLSEXT_TYPE_application_layer_protocol_negotiation
  {"application layer protocol negotiation",
    TLSEXT_TYPE_application_layer_protocol_negotiation},
#endif
#ifdef TLSEXT_TYPE_extended_master_secret
  {"extended master secret", TLSEXT_TYPE_extended_master_secret},
#endif
  {"key share", TLSEXT_TYPE_key_share},
  {"supported versions", TLSEXT_TYPE_supported_versions},
  {"psk", TLSEXT_TYPE_psk},
  {"psk kex modes", TLSEXT_TYPE_psk_kex_modes},
  {"certificate authorities", TLSEXT_TYPE_certificate_authorities},
  {"post handshake auth", TLSEXT_TYPE_post_handshake_auth},
  {NULL}
};

static STRINT_PAIR callback_types[] = 
{
  {"Supported Ciphersuite", SSL_SECOP_CIPHER_SUPPORTED},
  {"Shared Ciphersuite", SSL_SECOP_CIPHER_SHARED},
  {"Check Ciphersuite", SSL_SECOP_CIPHER_CHECK},
#ifndef OPENSSL_NO_DH
  {"Temp DH key bits", SSL_SECOP_TMP_DH},
#endif
  {"Supported Curve", SSL_SECOP_CURVE_SUPPORTED},
  {"Shared Curve", SSL_SECOP_CURVE_SHARED},
  {"Check Curve", SSL_SECOP_CURVE_CHECK},
  {"Supported Signature Algorithm", SSL_SECOP_SIGALG_SUPPORTED},
  {"Shared Signature Algorithm", SSL_SECOP_SIGALG_SHARED},
  {"Check Signature Algorithm", SSL_SECOP_SIGALG_CHECK},
  {"Signature Algorithm mask", SSL_SECOP_SIGALG_MASK},
  {"Certificate chain EE key", SSL_SECOP_EE_KEY},
  {"Certificate chain CA key", SSL_SECOP_CA_KEY},
  {"Peer Chain EE key", SSL_SECOP_PEER_EE_KEY},
  {"Peer Chain CA key", SSL_SECOP_PEER_CA_KEY},
  {"Certificate chain CA digest", SSL_SECOP_CA_MD},
  {"Peer chain CA digest", SSL_SECOP_PEER_CA_MD},
  {"SSL compression", SSL_SECOP_COMPRESSION},
  {"Session ticket", SSL_SECOP_TICKET},
  {NULL}
};

/* from rfc8446 4.2.3. + gost (https://tools.ietf.org/id/draft-smyshlyaev-tls12-gost-suites-04.html) */
static STRINT_PAIR signature_tls13_scheme_list[] = {
  {"rsa_pkcs1_sha1",         0x0201 /* TLSEXT_SIGALG_rsa_pkcs1_sha1 */},
  {"ecdsa_sha1",             0x0203 /* TLSEXT_SIGALG_ecdsa_sha1 */},
/*  {"rsa_pkcs1_sha224",       0x0301    TLSEXT_SIGALG_rsa_pkcs1_sha224}, not in rfc8446 */
/*  {"ecdsa_sha224",           0x0303    TLSEXT_SIGALG_ecdsa_sha224}      not in rfc8446 */
  {"rsa_pkcs1_sha256",       0x0401 /* TLSEXT_SIGALG_rsa_pkcs1_sha256 */},
  {"ecdsa_secp256r1_sha256", 0x0403 /* TLSEXT_SIGALG_ecdsa_secp256r1_sha256 */},
  {"rsa_pkcs1_sha384",       0x0501 /* TLSEXT_SIGALG_rsa_pkcs1_sha384 */},
  {"ecdsa_secp384r1_sha384", 0x0503 /* TLSEXT_SIGALG_ecdsa_secp384r1_sha384 */},
  {"rsa_pkcs1_sha512",       0x0601 /* TLSEXT_SIGALG_rsa_pkcs1_sha512 */},
  {"ecdsa_secp521r1_sha512", 0x0603 /* TLSEXT_SIGALG_ecdsa_secp521r1_sha512 */},
  {"rsa_pss_rsae_sha256",    0x0804 /* TLSEXT_SIGALG_rsa_pss_rsae_sha256 */},
  {"rsa_pss_rsae_sha384",    0x0805 /* TLSEXT_SIGALG_rsa_pss_rsae_sha384 */},
  {"rsa_pss_rsae_sha512",    0x0806 /* TLSEXT_SIGALG_rsa_pss_rsae_sha512 */},
  {"ed25519",                0x0807 /* TLSEXT_SIGALG_ed25519 */},
  {"ed448",                  0x0808 /* TLSEXT_SIGALG_ed448 */},
  {"rsa_pss_pss_sha256",     0x0809 /* TLSEXT_SIGALG_rsa_pss_pss_sha256 */},
  {"rsa_pss_pss_sha384",     0x080a /* TLSEXT_SIGALG_rsa_pss_pss_sha384 */},
  {"rsa_pss_pss_sha512",     0x080b /* TLSEXT_SIGALG_rsa_pss_pss_sha512 */},
  {"gostr34102001",          0xeded /* TLSEXT_SIGALG_gostr34102001_gostr3411 */},
  {"gostr34102012_256",      0xeeee /* TLSEXT_SIGALG_gostr34102012_256_gostr34112012_256 */},
  {"gostr34102012_512",      0xefef /* TLSEXT_SIGALG_gostr34102012_512_gostr34112012_512 */},
  {NULL}
}; 

/* from rfc5246 7.4.1.4.1. */
static STRINT_PAIR signature_tls12_alg_list[] = {
  {"anonymous", TLSEXT_signature_anonymous /* 0 */},
  {"RSA",       TLSEXT_signature_rsa       /* 1 */},
  {"DSA",       TLSEXT_signature_dsa       /* 2 */},
  {"ECDSA",     TLSEXT_signature_ecdsa     /* 3 */},
  {NULL}
};

/* from rfc5246 7.4.1.4.1. */
static STRINT_PAIR signature_tls12_hash_list[] = {
  {"none",   TLSEXT_hash_none   /* 0 */},
  {"MD5",    TLSEXT_hash_md5    /* 1 */},
  {"SHA1",   TLSEXT_hash_sha1   /* 2 */},
  {"SHA224", TLSEXT_hash_sha224 /* 3 */},
  {"SHA256", TLSEXT_hash_sha256 /* 4 */},
  {"SHA384", TLSEXT_hash_sha384 /* 5 */},
  {"SHA512", TLSEXT_hash_sha512 /* 6 */},
  {NULL}
};

static std::string Lookup(STRINT_PAIR* t, int v)
{
  for (; t->String; t++)
  {
    if (t->Value == v)
      return t->String;
  }
  return std::string();
}

#define CASE_AND_RET(v) case v: return #v

std::string Syncme::SSLProtocolName(int version)
{
  switch (version)
  {
    CASE_AND_RET(SSL2_VERSION);
    CASE_AND_RET(SSL3_VERSION);
    CASE_AND_RET(TLS1_VERSION);
    CASE_AND_RET(TLS1_1_VERSION);
    CASE_AND_RET(TLS1_2_VERSION);
    CASE_AND_RET(TLS1_3_VERSION);
    CASE_AND_RET(DTLS1_VERSION);
    CASE_AND_RET(DTLS1_2_VERSION);
    CASE_AND_RET(DTLS1_BAD_VER);
  }
  return std::to_string(version);
}

std::string Syncme::SSLContentType(int content_type)
{
  switch (content_type)
  {
    CASE_AND_RET(SSL3_RT_HEADER);
    CASE_AND_RET(SSL3_RT_INNER_CONTENT_TYPE);
    CASE_AND_RET(SSL3_RT_CHANGE_CIPHER_SPEC);
    CASE_AND_RET(SSL3_RT_ALERT);
    CASE_AND_RET(SSL3_RT_HANDSHAKE);
    CASE_AND_RET(SSL3_RT_APPLICATION_DATA);
  }
  return std::to_string(content_type);
}

std::string Syncme::SSLPacketDescr(
  int version
  , int content_type
  , const void* buf
  , size_t len
)
{
  std::string str;
  const unsigned char* bp = (const unsigned char*)buf;

  if (version == SSL3_VERSION
    || version == TLS1_VERSION
    || version == TLS1_1_VERSION
    || version == TLS1_2_VERSION
    || version == TLS1_3_VERSION
    || version == DTLS1_VERSION
    || version == DTLS1_BAD_VER
  )
  {
    if (content_type == SSL3_RT_ALERT)
    {
      str += " [alert] ";
      if (len == 2)
      {
        if (bp[0] == 1)
          str += "warning ";
        else if (bp[0] == 2)
          str += "fatal ";
        
        str += Lookup(alert_types, bp[1]);
      }
    }
    else if (content_type == SSL3_RT_HANDSHAKE)
    {
      if (len)
        str += Lookup(handshakes, bp[0]);
    }
  }
  return str;
}

std::string Syncme::TlsExtType(int type)
{
  return Lookup(tlsext_types, type);
}

std::string Syncme::SecurityCallbackType(int type)
{
  return Lookup(callback_types, type);
}

std::string Syncme::Tls13Scheme(int type)
{
  return Lookup(signature_tls13_scheme_list, type);
}

std::string Syncme::Tls12Alg(int type)
{
  return Lookup(signature_tls12_alg_list, type);
}

std::string Syncme::Tls12Hash(int type)
{
  return Lookup(signature_tls12_hash_list, type);
}

static STRINT_PAIR ssl_errors[] = 
{
  {"SSL_ERROR_NONE", SSL_ERROR_NONE},
  {"SSL_ERROR_SSL", SSL_ERROR_SSL},
  {"SSL_ERROR_WANT_READ", SSL_ERROR_WANT_READ},
  {"SSL_ERROR_WANT_WRITE", SSL_ERROR_WANT_WRITE},
  {"SSL_ERROR_WANT_X509_LOOKUP", SSL_ERROR_WANT_X509_LOOKUP},
  {"SSL_ERROR_SYSCALL", SSL_ERROR_SYSCALL},
  {"SSL_ERROR_ZERO_RETURN", SSL_ERROR_ZERO_RETURN},
  {"SSL_ERROR_WANT_CONNECT", SSL_ERROR_WANT_CONNECT},
  {"SSL_ERROR_WANT_ACCEPT", SSL_ERROR_WANT_ACCEPT},
  {"SSL_ERROR_WANT_ASYNC", SSL_ERROR_WANT_ASYNC},
  {"SSL_ERROR_WANT_ASYNC_JOB", SSL_ERROR_WANT_ASYNC_JOB},
  {"SSL_ERROR_WANT_CLIENT_HELLO_CB", SSL_ERROR_WANT_CLIENT_HELLO_CB},
  {"SSL_ERROR_WANT_RETRY_VERIFY", SSL_ERROR_WANT_RETRY_VERIFY},
};

std::string Syncme::SslError(int code)
{
  std::string str = Lookup(ssl_errors, code);

  if (str.empty())
    str = std::to_string(code);

  return str;
}

std::string Syncme::GetBioError()
{
#ifdef _WIN32
  int e = GetLastError();
#endif  

  BIO* bio = BIO_new(BIO_s_mem());
  ERR_print_errors(bio);

  char* buffer = nullptr;
  size_t len = BIO_get_mem_data(bio, &buffer);

  std::string errorString(buffer, len);

#ifdef USE_LOGME
  if (errorString.empty())
    errorString = LRESULT_STR(e);
#endif    

  while (!errorString.empty())
  {
    auto n = errorString.size();
    auto c = errorString[n - 1];
    if (c != '\r' && c != '\n')
      break;

    errorString = errorString.substr(0, n - 1);
  }

  BIO_free(bio);
  return errorString;
}
