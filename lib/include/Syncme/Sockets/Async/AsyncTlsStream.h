#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>

#include <Syncme/Api.h>
#include <Syncme/Sockets/Async/AsyncStream.h>
#include <Syncme/Sockets/Async/AsyncWriteQueue.h>
#include <Syncme/Sockets/OsslCompat.h>

namespace Syncme
{
  namespace Sockets
  {
    namespace Async
    {
      class AsyncTlsStream;

      using AsyncTlsStreamPtr = std::shared_ptr<AsyncTlsStream>;

      class AsyncTlsStream : public AsyncStream
        , public std::enable_shared_from_this<AsyncTlsStream>
      {
        AsyncStreamPtr LowerStream;
        void* Context;
        SSL* Ssl;
        bool OwnSsl;

        mutable std::recursive_mutex Lock;
        std::deque<Result> PendingResults;
        std::deque<IO::BufferPtr> AdoptedPlainBuffers;
        size_t AdoptedPlainOffset;
        AsyncWriteQueue LowerWriter;

        bool Removing;
        bool HandshakeCompleted;
        bool LowerReadPending;
        bool LowerReadClosed;
        bool TlsReadClosed;
        bool ShutdownPending;
        bool ShutdownCompleted;
        bool LowerSendShutdownCompleted;

        IO::BufferPtr PlainReadBuffer;
        bool PlainReadPending;

        BufferChain PlainWriteBuffers;
        size_t PlainWriteOffset;
        size_t PlainWriteSize;
        bool PlainWritePending;

      public:
        SINCMELNK AsyncTlsStream(
          AsyncStreamPtr lowerStream
          , SSL* ssl
          , bool ownSsl = false
          , void* context = nullptr
        );

        SINCMELNK ~AsyncTlsStream() override;

        SINCMELNK Socket* GetSocket() const override;
        SINCMELNK void* GetContext() const override;

        SINCMELNK bool StartRead(IO::BufferPtr buffer) override;
        SINCMELNK bool StartWrite(const BufferChain& buffers) override;
        SINCMELNK bool ShutdownSend() override;
        SINCMELNK void Close() override;

        SINCMELNK bool StartHandshake();
        SINCMELNK bool IsHandshakeCompleted() const;
        SINCMELNK bool IsShutdownCompleted() const;

        SINCMELNK bool ProcessLowerResult(const Result& result);
        SINCMELNK bool PopPendingResult(Result& result);
        SINCMELNK bool HasPendingResult() const;

        SINCMELNK AsyncStreamPtr GetLowerStream() const;
        SINCMELNK SSL* GetSsl() const;

      private:
        bool InitializeBio();
        bool AdoptPlaintextPending();
        bool DeliverAdoptedPlaintext();
        bool Drive();
        bool DriveHandshake();
        bool DrivePlainRead();
        bool DrivePlainWrite();
        bool DriveShutdown();
        bool DrainEncryptedOutput();
        bool StartLowerRead();
        bool FeedEncryptedInput(IO::BufferPtr buffer, size_t bytes);
        bool CompleteLowerWrite(size_t bytes);
        bool CompleteLowerShutdown();

        bool QueueResult(
          Operation op
          , IO::BufferPtr buffer
          , size_t bytes
          , int error
        );

        bool QueueError(int error);
        bool QueueReadClosed();
        bool IsWantIO(int error) const;
        int GetSslError(int rc) const;
        void ResetPlainWrite();
      };
    }
  }
}
