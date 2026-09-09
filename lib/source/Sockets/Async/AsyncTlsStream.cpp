#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/Async/AsyncTlsStream.h>
#include <Syncme/Sockets/SSLHelpers.h>
#include <Syncme/Sockets/Socket.h>

using namespace Syncme;
using namespace Syncme::Sockets::Async;
namespace IO = Syncme::Sockets::IO;

namespace
{
  constexpr size_t ENCRYPTED_READ_SIZE = IO::BUFFER_SIZE;
  constexpr size_t ENCRYPTED_CHUNK_SIZE = IO::BUFFER_SIZE;

  bool IsSslWantIO(int error)
  {
    return error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE;
  }

  int ConvertSslError(int error)
  {
    if (error == SSL_ERROR_ZERO_RETURN)
      return 0;

    if (IsSslWantIO(error) || error == SSL_ERROR_NONE)
      return 0;

    return error;
  }
}

AsyncTlsStream::AsyncTlsStream(
  AsyncStreamPtr lowerStream
  , SSL* ssl
  , bool ownSsl
  , void* context
)
  : LowerStream(std::move(lowerStream))
  , Context(context)
  , Ssl(ssl)
  , OwnSsl(ownSsl)
  , ResultNotified(false)
  , AdoptedPlainOffset(0)
  , Removing(false)
  , HandshakeStarted(false)
  , HandshakeCompleted(false)
  , HandshakeResultQueued(false)
  , LowerReadPending(false)
  , LowerReadClosed(false)
  , TlsReadClosed(false)
  , ShutdownPending(false)
  , ShutdownCompleted(false)
  , LowerSendShutdownCompleted(false)
  , PlainReadPending(false)
  , PlainWriteOffset(0)
  , PlainWriteSize(0)
  , PlainWritePending(false)
  , PlainWriteNeedsRead(false)
{
  if (Context == nullptr && LowerStream != nullptr)
    Context = LowerStream->GetContext();

  LowerWriter.Attach(LowerStream);

  if (Ssl != nullptr)
  {
    SSL_set_mode(
      Ssl
      , SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER
    );

    HandshakeCompleted = SSL_is_init_finished(Ssl) != 0;
    HandshakeStarted = HandshakeCompleted;

    if (HandshakeCompleted)
      AdoptPlaintextPending();

    if (!InitializeBio())
      LastError = "InitializeBio failed";
  }
}

AsyncTlsStream::~AsyncTlsStream()
{
  Close();

  if (OwnSsl && Ssl != nullptr)
  {
    SSL_free(Ssl);
    Ssl = nullptr;
  }
}

Socket* AsyncTlsStream::GetSocket() const
{
  return LowerStream != nullptr ? LowerStream->GetSocket() : nullptr;
}

void* AsyncTlsStream::GetContext() const
{
  return Context;
}

bool AsyncTlsStream::StartRead(IO::BufferPtr buffer)
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  if (Removing || TlsReadClosed || PlainReadPending)
    return false;

  if (buffer == nullptr || buffer->empty())
    return false;

  PlainReadBuffer = buffer;
  PlainReadPending = true;

  return Drive();
}

bool AsyncTlsStream::StartWrite(const BufferChain& buffers)
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  if (Removing || PlainWritePending || buffers.IsEmpty())
    return false;

  PlainWriteBuffers = buffers;
  PlainWriteOffset = 0;
  PlainWriteSize = buffers.Size();
  PlainWritePending = true;
  PlainWriteNeedsRead = false;

  return Drive();
}

bool AsyncTlsStream::ShutdownSend()
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  if (Removing)
    return false;

  if (ShutdownCompleted)
    return true;

  ShutdownPending = true;
  return Drive();
}

void AsyncTlsStream::Close()
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  if (Removing)
    return;

  Removing = true;
  ShutdownPending = false;
  PendingResults.clear();
  ResultNotified = false;
  ResultSink = nullptr;
  AdoptedPlainBuffers.clear();
  AdoptedPlainOffset = 0;
  LowerWriter.Clear();
  PlainReadBuffer.reset();
  PlainReadPending = false;
  ResetPlainWrite();
}

bool AsyncTlsStream::StartHandshake()
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  if (Removing)
  {
    LastError = "failed to start handshake: stream is removing";
    return false;
  }

  if (Ssl == nullptr)
  {
    LastError = "failed to start handshake: SSL object is null";
    return false;
  }

  HandshakeStarted = true;
  return Drive();
}

bool AsyncTlsStream::AdoptPendingLowerRead()
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  if (Removing)
  {
    LastError = "pending lower read adoption failed: stream is removing";
    return false;
  }

  if (LowerReadClosed)
  {
    LastError = "pending lower read adoption failed: lower read is closed";
    return false;
  }

  if (LowerReadPending)
  {
    LastError = "pending lower read adoption failed: lower read is already pending";
    return false;
  }

  LowerReadPending = true;
  return true;
}

bool AsyncTlsStream::FeedEncryptedInput(const void* data, size_t bytes)
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  if (Removing || Ssl == nullptr)
  {
    LastError = Removing
      ? "encrypted input feed failed: stream is removing"
      : "encrypted input feed failed: SSL object is null";
    LogE(
      "async TLS encrypted input feed failed: removing=%i ssl=%p bytes=%zu"
      , Removing ? 1 : 0
      , Ssl
      , bytes
    );
    return false;
  }

  if (bytes == 0)
    return true;

  if (data == nullptr)
  {
    LastError = "encrypted input feed failed: data is null";
    LogE("async TLS encrypted input feed failed: data is null bytes=%zu", bytes);
    return false;
  }

  BIO* rbio = SSL_get_rbio(Ssl);
  if (rbio == nullptr)
  {
    LastError = "encrypted input feed failed: rbio is null";
    LogE("async TLS encrypted input feed failed: rbio is null ssl=%p bytes=%zu", Ssl, bytes);
    return false;
  }

  size_t offset = 0;
  const char* ptr = static_cast<const char*>(data);

  while (offset < bytes)
  {
    int rc = BIO_write(
      rbio
      , ptr + offset
      , int(bytes - offset)
    );

    if (rc <= 0)
    {
      if (BIO_should_retry(rbio))
        continue;

      LastError = "encrypted input feed failed: BIO_write failed: rc="
        + std::to_string(rc);
      LogE(
        "async TLS encrypted input feed failed: BIO_write failed ssl=%p rbio=%p bytes=%zu offset=%zu rc=%i"
        , Ssl
        , rbio
        , bytes
        , offset
        , rc
      );
      return false;
    }

    offset += size_t(rc);
  }

  if (!HandshakeStarted)
    return true;

  if (!Drive())
  {
    LogE("async TLS encrypted input feed failed: TLS drive failed after prefetch ssl=%p bytes=%zu", Ssl, bytes);
    return false;
  }

  return true;
}

bool AsyncTlsStream::IsHandshakeCompleted() const
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  return HandshakeCompleted;
}

bool AsyncTlsStream::IsShutdownCompleted() const
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  return ShutdownCompleted && LowerSendShutdownCompleted;
}

AsyncTlsStreamDiagnostics AsyncTlsStream::GetDiagnostics() const
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  AsyncTlsStreamDiagnostics diagnostics{};
  diagnostics.Removing = Removing;
  diagnostics.HandshakeStarted = HandshakeStarted;
  diagnostics.HandshakeCompleted = HandshakeCompleted;
  diagnostics.HandshakeResultQueued = HandshakeResultQueued;
  diagnostics.LowerReadPending = LowerReadPending;
  diagnostics.LowerReadClosed = LowerReadClosed;
  diagnostics.TlsReadClosed = TlsReadClosed;
  diagnostics.ShutdownPending = ShutdownPending;
  diagnostics.ShutdownCompleted = ShutdownCompleted;
  diagnostics.LowerSendShutdownCompleted = LowerSendShutdownCompleted;
  diagnostics.PlainReadPending = PlainReadPending;
  diagnostics.PlainWritePending = PlainWritePending;
  diagnostics.PlainWriteNeedsRead = PlainWriteNeedsRead;
  diagnostics.LowerWritePending = LowerWriter.IsWriting();
  diagnostics.LowerWriteBytes = LowerWriter.Size();
  diagnostics.LowerWriteCount = LowerWriter.Count();
  diagnostics.PendingResultCount = PendingResults.size();
  diagnostics.LastError = LastError;
  return diagnostics;
}

bool AsyncTlsStream::ProcessLowerResult(const Result& result)
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  if (Removing)
    return false;

  if (result.Stream != LowerStream)
    return false;

  switch (result.Op)
  {
  case Operation::Read:
    LowerReadPending = false;
    if (!FeedEncryptedInput(result.Buffer, result.Bytes))
      return false;
    return Drive();

  case Operation::Write:
    if (!CompleteLowerWrite(result.Bytes))
      return false;
    return Drive();

  case Operation::ReadClosed:
    LowerReadPending = false;
    LowerReadClosed = true;
    TlsReadClosed = true;

    if (PlainWriteNeedsRead)
    {
      LastError = "lower read closed while TLS write requires input";
      PlainReadPending = false;
      PlainReadBuffer.reset();
      ResetPlainWrite();
      return QueueError(SSL_ERROR_SYSCALL);
    }

    if (!PlainReadPending)
      return true;

    PlainReadPending = false;
    PlainReadBuffer.reset();
    return QueueReadClosed();

  case Operation::Error:
    LowerReadPending = false;
    LowerReadClosed = true;
    LastError = "lower stream error=" + std::to_string(result.Error);
    return QueueError(result.Error);

  default:
    break;
  }

  return true;
}

bool AsyncTlsStream::PopPendingResult(Result& result)
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  if (PendingResults.empty())
  {
    ResultNotified = false;
    return false;
  }

  result = PendingResults.front();
  PendingResults.pop_front();

  // Re-arm the ready notification once the owner has drained everything, so a
  // later QueueResult wakes it again.
  if (PendingResults.empty())
    ResultNotified = false;

  return true;
}

bool AsyncTlsStream::HasPendingResult() const
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  return !PendingResults.empty();
}

void AsyncTlsStream::SetResultSink(std::function<void()> sink)
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  ResultSink = std::move(sink);
  ResultNotified = false;

  if (ResultSink && !PendingResults.empty())
  {
    ResultNotified = true;
    ResultSink();
  }
}

void AsyncTlsStream::NotifyResultReady()
{
  // Caller holds Lock.
  if (ResultNotified || !ResultSink)
    return;

  ResultNotified = true;
  ResultSink();
}

AsyncStreamPtr AsyncTlsStream::GetLowerStream() const
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  return LowerStream;
}

SSL* AsyncTlsStream::GetSsl() const
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  return Ssl;
}

bool AsyncTlsStream::InitializeBio()
{
  if (Ssl == nullptr)
    return false;

  BIO* rbio = BIO_new(BIO_s_mem());
  BIO* wbio = BIO_new(BIO_s_mem());

  if (rbio == nullptr || wbio == nullptr)
  {
    if (rbio != nullptr)
      BIO_free(rbio);

    if (wbio != nullptr)
      BIO_free(wbio);

    return false;
  }

  BIO_set_mem_eof_return(rbio, -1);
  BIO_set_mem_eof_return(wbio, -1);
  SSL_set_bio(Ssl, rbio, wbio);
  return true;
}

bool AsyncTlsStream::AdoptPlaintextPending()
{
  if (Ssl == nullptr)
    return false;

  for (;;)
  {
    if (SSL_has_pending(Ssl) == 0 && SSL_pending(Ssl) <= 0)
      return true;

    auto buffer = std::make_shared<IO::Buffer>();
    if (buffer == nullptr)
      return false;

    buffer->resize(ENCRYPTED_READ_SIZE);

    ERR_clear_error();
    int rc = SSL_read(Ssl, buffer->data(), int(buffer->size()));
    if (rc <= 0)
    {
      int error = GetSslError(rc);
      if (IsWantIO(error))
        return true;

      if (error == SSL_ERROR_ZERO_RETURN)
      {
        TlsReadClosed = true;
        return true;
      }

      SetSslError("SSL_read adopted plaintext", rc, error);
      return false;
    }

    buffer->resize(size_t(rc));
    AdoptedPlainBuffers.push_back(buffer);
  }
}

bool AsyncTlsStream::DeliverAdoptedPlaintext()
{
  if (!PlainReadPending || PlainReadBuffer == nullptr)
    return true;

  if (AdoptedPlainBuffers.empty())
    return true;

  IO::BufferPtr source = AdoptedPlainBuffers.front();
  if (source == nullptr || AdoptedPlainOffset >= source->size())
  {
    AdoptedPlainBuffers.pop_front();
    AdoptedPlainOffset = 0;
    return DeliverAdoptedPlaintext();
  }

  size_t available = source->size() - AdoptedPlainOffset;
  size_t capacity = PlainReadBuffer->size();
  size_t size = std::min(available, capacity);

  std::memcpy(
    PlainReadBuffer->data()
    , source->data() + AdoptedPlainOffset
    , size
  );

  AdoptedPlainOffset += size;

  if (AdoptedPlainOffset >= source->size())
  {
    AdoptedPlainBuffers.pop_front();
    AdoptedPlainOffset = 0;
  }

  IO::BufferPtr buffer = PlainReadBuffer;
  buffer->resize(size);
  PlainReadBuffer.reset();
  PlainReadPending = false;

  return QueueResult(Operation::Read, buffer, size, 0);
}

bool AsyncTlsStream::Drive()
{
  if (Ssl == nullptr)
  {
    LastError = "TLS drive failed: SSL object is null";
    return false;
  }

  if (LowerStream == nullptr)
  {
    LastError = "TLS drive failed: lower stream is null";
    return false;
  }

  if (!HandshakeCompleted)
  {
    if (!HandshakeStarted)
      return true;

    if (!DriveHandshake())
      return false;

    if (!HandshakeCompleted)
    {
      if (!DrainEncryptedOutput())
        return false;

      return StartLowerRead();
    }
  }

  if (PlainReadPending)
  {
    if (!DrivePlainRead())
      return false;
  }

  if (PlainWritePending)
  {
    if (!DrivePlainWrite())
      return false;
  }

  if (ShutdownPending && !ShutdownCompleted)
  {
    if (!DriveShutdown())
      return false;
  }

  if (!DrainEncryptedOutput())
    return false;

  if (!CompleteLowerShutdown())
    return false;

  return StartLowerRead();
}

bool AsyncTlsStream::DriveHandshake()
{
  if (HandshakeCompleted)
    return true;

  ERR_clear_error();
  int rc = SSL_do_handshake(Ssl);
  if (rc == 1)
  {
    HandshakeCompleted = true;
    LastError.clear();
    return QueueHandshakeCompleted();
  }

  int error = GetSslError(rc);
  if (IsWantIO(error))
    return true;

  SetSslError("SSL_do_handshake", rc, error);
  return QueueError(ConvertSslError(error));
}

bool AsyncTlsStream::DrivePlainRead()
{
  if (!PlainReadPending || PlainReadBuffer == nullptr)
    return true;

  if (!DeliverAdoptedPlaintext())
    return false;

  if (!PlainReadPending || PlainReadBuffer == nullptr)
    return true;

  ERR_clear_error();
  int rc = SSL_read(
    Ssl
    , PlainReadBuffer->data()
    , int(PlainReadBuffer->size())
  );

  if (rc > 0)
  {
    IO::BufferPtr buffer = PlainReadBuffer;
    buffer->resize(size_t(rc));
    PlainReadBuffer.reset();
    PlainReadPending = false;
    return QueueResult(Operation::Read, buffer, size_t(rc), 0);
  }

  int error = GetSslError(rc);
  if (error == SSL_ERROR_ZERO_RETURN)
  {
    TlsReadClosed = true;
    PlainReadBuffer.reset();
    PlainReadPending = false;
    return QueueReadClosed();
  }

  if (IsWantIO(error))
    return true;

  SetSslError("SSL_read", rc, error);
  PlainReadBuffer.reset();
  PlainReadPending = false;
  return QueueError(ConvertSslError(error));
}

bool AsyncTlsStream::DrivePlainWrite()
{
  if (!PlainWritePending)
    return true;

  if (PlainWriteOffset >= PlainWriteSize)
  {
    if (!LowerWriter.IsIdle())
      return true;

    size_t total = PlainWriteSize;
    ResetPlainWrite();
    return QueueResult(Operation::Write, nullptr, total, 0);
  }

  size_t skip = PlainWriteOffset;

  for (const auto& view : PlainWriteBuffers.GetViews())
  {
    if (skip >= view.Size)
    {
      skip -= view.Size;
      continue;
    }

    const char* data = view.Buffer->data() + view.Offset + skip;
    size_t size = view.Size - skip;

    ERR_clear_error();
    int rc = SSL_write(Ssl, data, int(size));
    if (rc > 0)
    {
      PlainWriteNeedsRead = false;
      PlainWriteOffset += size_t(rc);
      if (!DrainEncryptedOutput())
        return false;

      if (size_t(rc) < size)
        return true;

      skip = 0;
      continue;
    }

    int error = GetSslError(rc);
    PlainWriteNeedsRead = error == SSL_ERROR_WANT_READ;
    if (IsWantIO(error))
      return DrainEncryptedOutput();

    SetSslError("SSL_write", rc, error);
    ResetPlainWrite();
    return QueueError(ConvertSslError(error));
  }

  if (PlainWriteOffset >= PlainWriteSize && LowerWriter.IsIdle())
  {
    size_t total = PlainWriteSize;
    ResetPlainWrite();
    return QueueResult(Operation::Write, nullptr, total, 0);
  }

  return true;
}

bool AsyncTlsStream::DriveShutdown()
{
  ERR_clear_error();
  int rc = SSL_shutdown(Ssl);

  if (rc == 1)
  {
    ShutdownCompleted = true;
    ShutdownPending = false;
    return true;
  }

  if (rc == 0)
    return true;

  int error = GetSslError(rc);
  if (IsWantIO(error))
    return true;

  SetSslError("SSL_shutdown", rc, error);
  return QueueError(ConvertSslError(error));
}

bool AsyncTlsStream::DrainEncryptedOutput()
{
  BIO* wbio = SSL_get_wbio(Ssl);
  if (wbio == nullptr)
  {
    LastError = "failed to drain encrypted output: wbio is null";
    return false;
  }

  for (;;)
  {
    size_t pending = size_t(BIO_ctrl_pending(wbio));
    if (pending == 0)
      return true;

    size_t size = std::min(pending, ENCRYPTED_CHUNK_SIZE);
    IO::BufferPtr buffer = std::make_shared<IO::Buffer>();
    if (buffer == nullptr)
    {
      LastError = "failed to drain encrypted output: buffer allocation failed";
      return false;
    }

    buffer->resize(size);

    int rc = BIO_read(wbio, buffer->data(), int(buffer->size()));
    if (rc <= 0)
    {
      if (BIO_should_retry(wbio))
        return true;

      LastError = "failed to drain encrypted output: BIO_read failed";
      return false;
    }

    buffer->resize(size_t(rc));

    if (!LowerWriter.Push(buffer))
    {
      LastError = "failed to start encrypted lower write";
      return false;
    }
  }
}

bool AsyncTlsStream::StartLowerRead()
{
  if (LowerReadPending || LowerReadClosed || Removing)
    return true;

  if (!HandshakeStarted && !HandshakeCompleted)
    return true;

  if (!PlainReadPending
    && !PlainWriteNeedsRead
    && HandshakeCompleted
    && !ShutdownPending)
  {
    return true;
  }

  IO::BufferPtr buffer = std::make_shared<IO::Buffer>();
  if (buffer == nullptr)
  {
    LastError = "failed to start encrypted lower read: buffer allocation failed";
    return false;
  }

  buffer->resize(ENCRYPTED_READ_SIZE);

  if (!LowerStream->StartRead(buffer))
  {
    LastError = "failed to start encrypted lower read";
    return false;
  }

  LowerReadPending = true;
  return true;
}

bool AsyncTlsStream::FeedEncryptedInput(IO::BufferPtr buffer, size_t bytes)
{
  if (buffer == nullptr || bytes == 0)
    return true;

  BIO* rbio = SSL_get_rbio(Ssl);
  if (rbio == nullptr)
  {
    LastError = "encrypted buffer feed failed: rbio is null";
    LogE("async TLS encrypted buffer feed failed: rbio is null ssl=%p bytes=%zu", Ssl, bytes);
    return false;
  }

  size_t offset = 0;
  while (offset < bytes)
  {
    int rc = BIO_write(
      rbio
      , buffer->data() + offset
      , int(bytes - offset)
    );

    if (rc <= 0)
    {
      if (BIO_should_retry(rbio))
        continue;

      LastError = "encrypted buffer feed failed: BIO_write failed: rc="
        + std::to_string(rc);
      LogE(
        "async TLS encrypted buffer feed failed: BIO_write failed ssl=%p rbio=%p bytes=%zu offset=%zu rc=%i"
        , Ssl
        , rbio
        , bytes
        , offset
        , rc
      );
      return false;
    }

    offset += size_t(rc);
  }

  return true;
}

bool AsyncTlsStream::CompleteLowerWrite(size_t bytes)
{
  if (!LowerWriter.OnWriteCompleted(bytes))
  {
    LastError = "encrypted lower write completion mismatch: bytes="
      + std::to_string(bytes);
    return false;
  }

  return CompleteLowerShutdown();
}

bool AsyncTlsStream::CompleteLowerShutdown()
{
  if (!ShutdownCompleted || !LowerWriter.IsIdle())
    return true;

  if (LowerSendShutdownCompleted)
    return true;

  if (LowerStream == nullptr)
  {
    LastError = "failed to complete TLS shutdown: lower stream is null";
    return false;
  }

  if (!LowerStream->ShutdownSend())
  {
    LastError = "failed to complete TLS shutdown: lower send shutdown failed";
    return false;
  }

  LowerSendShutdownCompleted = true;

  return true;
}

bool AsyncTlsStream::QueueResult(
  Operation op
  , IO::BufferPtr buffer
  , size_t bytes
  , int error
)
{
  Result result;
  result.Stream = shared_from_this();
  result.Context = Context;
  result.Op = op;
  result.Buffer = buffer;
  result.Bytes = bytes;
  result.Error = error;

  PendingResults.push_back(result);
  NotifyResultReady();
  return true;
}

bool AsyncTlsStream::QueueError(int error)
{
  if (error == 0)
    error = SSL_ERROR_SSL;

  if (LastError.empty())
    LastError = "TLS protocol error=" + std::to_string(error);

  return QueueResult(Operation::Error, nullptr, 0, error);
}

bool AsyncTlsStream::QueueReadClosed()
{
  return QueueResult(Operation::ReadClosed, nullptr, 0, 0);
}

bool AsyncTlsStream::QueueHandshakeCompleted()
{
  if (HandshakeResultQueued)
    return true;

  HandshakeResultQueued = true;
  return QueueResult(Operation::Handshake, nullptr, 0, 0);
}

bool AsyncTlsStream::IsWantIO(int error) const
{
  return IsSslWantIO(error);
}

int AsyncTlsStream::GetSslError(int rc) const
{
  if (Ssl == nullptr)
    return SSL_ERROR_SSL;

  return SSL_get_error(Ssl, rc);
}

void AsyncTlsStream::SetSslError(
  const char* operation
  , int rc
  , int sslError
)
{
  LastError = operation ? operation : "TLS operation";
  LastError += " failed: rc=" + std::to_string(rc);
  LastError += ", ssl_error=" + std::to_string(sslError);

  for (;;)
  {
    unsigned long queueError = ERR_get_error();
    if (queueError == 0)
      break;

    char buffer[256]{};
    ERR_error_string_n(queueError, buffer, sizeof(buffer));
    LastError += ", openssl=";
    LastError += buffer;
  }
}

void AsyncTlsStream::ResetPlainWrite()
{
  PlainWriteBuffers.Clear();
  PlainWriteOffset = 0;
  PlainWriteSize = 0;
  PlainWritePending = false;
  PlainWriteNeedsRead = false;
}
