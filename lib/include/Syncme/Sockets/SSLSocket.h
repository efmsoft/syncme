#pragma once

#include <cstdint>
#include <vector>

#include <Syncme/Api.h>
#include <Syncme/Sockets/Socket.h>
#include <Syncme/Sockets/OsslCompat.h>


namespace Syncme
{
  struct SSLSocket : public Socket
  {
    std::mutex SslLock;
    SSL* Ssl;
    bool AsyncTlsAutoHandshake;
    std::vector<uint8_t> AsyncTlsPrefetch;

  public:
    SINCMELNK SSLSocket(SocketPair* pair, SSL* ssl);
    SINCMELNK ~SSLSocket();

    SINCMELNK void Shutdown() override;

    SINCMELNK SKT_ERROR Ossl2SktError(int ret) override;
    SINCMELNK int GetFD() const override;
    SINCMELNK void LogIoError(const char* fn, const char* text) override;
    SINCMELNK std::string GetProtocol() const override;

  private:
    int InternalWrite(const void* buffer, size_t size, int timeout) override;
    int InternalRead(void* buffer, size_t size, int timeout) override;
    int ReadPending(void* buffer, size_t size, int i);
    int TranslateSSLError(int n, const char* method);
  };
}