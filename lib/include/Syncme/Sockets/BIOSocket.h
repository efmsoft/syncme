#pragma once

#include <mutex>

#include <Syncme/Api.h>
#include <Syncme/Sockets/Socket.h>
#include <openssl/bio.h>

namespace Syncme
{
  struct BIOSocket : public Socket
  {
    std::mutex BioLock;
    BIO* Bio;

  public:
    SINCMELNK BIOSocket(SocketPair* pair);
    SINCMELNK ~BIOSocket();

    SINCMELNK int Read(void* buffer, size_t size, int timeout) override;
    SINCMELNK void Shutdown() override;

    SINCMELNK virtual SKT_ERROR Ossl2SktError(int ret) const override;
    SINCMELNK int GetFD() const override;
    SINCMELNK void LogIoError(const char* fn, const char* text) override;

    SINCMELNK bool Attach(int socket, bool enableClose = true) override;

  private:
    int InternalWrite(const void* buffer, size_t size, int timeout) override;
    int InternalRead(void* buffer, size_t size, int timeout);
  };
}