#pragma once

#include <Syncme/Sockets/Socket.h>
#include <openssl/ssl.h>

namespace Syncme
{
  struct SSLSocket : public Socket
  {
    std::mutex SslLock;
    SSL* Ssl;

  public:
    SSLSocket(SocketPair* pair, SSL* ssl);
    ~SSLSocket();

    int Read(void* buffer, size_t size, int timeout) override;
    void Shutdown() override;

    SKT_ERROR GetError(int ret) const override;
    int GetFD() const override;
    void LogIoError(const char* fn, const char* text) override;

  private:
    int InternalWrite(const void* buffer, size_t size, int timeout) override;
  };
}