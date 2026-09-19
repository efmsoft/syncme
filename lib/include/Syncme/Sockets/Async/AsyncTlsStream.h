#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

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

      struct AsyncTlsStreamDiagnostics
      {
        bool Removing;
        bool HandshakeStarted;
        bool HandshakeCompleted;
        bool HandshakeResultQueued;
        bool LowerReadPending;
        bool LowerReadClosed;
        bool TlsReadClosed;
        bool ShutdownPending;
        bool ShutdownCompleted;
        bool LowerSendShutdownCompleted;
        bool PlainReadPending;
        bool PlainWritePending;
        bool PlainWriteNeedsRead;
        bool LowerWritePending;
        size_t LowerWriteBytes;
        size_t LowerWriteCount;
        size_t PendingResultCount;
        std::string LastError;
      };

      class AsyncTlsStream : public AsyncStream
        , public std::enable_shared_from_this<AsyncTlsStream>
      {
        AsyncStreamPtr LowerStream;
        void* Context;
        SSL* Ssl;
        bool OwnSsl;

        mutable std::recursive_mutex Lock;
        std::deque<Result> PendingResults;

        // Invoked (under Lock) when PendingResults transitions from drained to
        // non-empty, so the owner can service this stream on demand instead of
        // polling every stream every loop iteration. ResultNotified suppresses
        // repeat calls until PopPendingResult drains the queue again.
        std::function<void()> ResultSink;
        bool ResultNotified;

        std::deque<IO::BufferPtr> AdoptedPlainBuffers;
        size_t AdoptedPlainOffset;
        AsyncWriteQueue LowerWriter;

        bool Removing;
        bool HandshakeStarted;
        bool HandshakeCompleted;
        bool HandshakeResultQueued;
        bool LowerReadPending;
        bool LowerReadClosed;
        bool TlsReadClosed;
        bool ShutdownPending;
        bool ShutdownCompleted;
        bool LowerSendShutdownCompleted;

        // Reused across lower-layer encrypted reads (StartLowerRead), same
        // reasoning as HTTP1_1::ServerReadBuffer: FeedEncryptedInput() only
        // ever copies this buffer's bytes into OpenSSL's rbio via
        // BIO_write(), nothing keeps a reference to the buffer object
        // afterward, so growing the same allocation back to full size on
        // each read is safe and avoids a fresh ENCRYPTED_READ_SIZE alloc/
        // free pair on every read (VTune, 2026-09).
        IO::BufferPtr LowerReadBuffer;

        // Buffers handed to LowerWriter (DrainEncryptedOutput) are reclaimed
        // here once their write completes (CompleteLowerWrite), instead of
        // freeing and reallocating a fresh ENCRYPTED_CHUNK_SIZE buffer on
        // every SSL_write's worth of ciphertext -- unlike LowerReadBuffer,
        // these buffers are handed off live to the write queue, so more than
        // one can be outstanding at a time and a single reused member isn't
        // enough; a small capped free list is (VTune, 2026-09).
        IO::BufferList FreeEncryptedWriteBuffers;

        IO::BufferPtr PlainReadBuffer;
        bool PlainReadPending;

        BufferChain PlainWriteBuffers;
        size_t PlainWriteOffset;
        size_t PlainWriteSize;
        bool PlainWritePending;
        bool PlainWriteNeedsRead;
        std::string LastError;

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
        SINCMELNK bool AdoptPendingLowerRead();
        SINCMELNK bool FeedEncryptedInput(const void* data, size_t bytes);
        SINCMELNK bool IsHandshakeCompleted() const;
        SINCMELNK bool IsShutdownCompleted() const;
        SINCMELNK AsyncTlsStreamDiagnostics GetDiagnostics() const;

        SINCMELNK bool ProcessLowerResult(const Result& result);
        SINCMELNK bool PopPendingResult(Result& result);
        SINCMELNK bool HasPendingResult() const;

        // Register (or clear, with nullptr) the ready notification callback.
        // The callback runs while the internal Lock is held; it must not call
        // back into this stream.
        SINCMELNK void SetResultSink(std::function<void()> sink);

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
        bool QueueHandshakeCompleted();
        void NotifyResultReady();
        bool IsWantIO(int error) const;
        int GetSslError(int rc) const;
        void SetSslError(const char* operation, int rc, int sslError);
        void ResetPlainWrite();
      };
    }
  }
}
