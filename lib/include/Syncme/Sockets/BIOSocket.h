#pragma once

#include <mutex>

#include <Syncme/Sockets/Socket.h>
#include <openssl/bio.h>

namespace Syncme
{
  struct BIOSocket : public Socket
  {
    std::mutex BioLock;
    BIO* Bio;

  public:
    BIOSocket(SocketPair* pair);
    ~BIOSocket();

    int Read(void* buffer, size_t size, int timeout) override;
    void Shutdown() override;

    SKT_ERROR GetError(int ret) const override;
    int GetFD() const override;
    void LogIoError(const char* fn, const char* text) override;

    bool Attach(int socket, bool enableClose = true) override;

  private:
    int InternalWrite(const void* buffer, size_t size, int timeout) override;
    int InternalRead(void* buffer, size_t size, int timeout);
  };
}