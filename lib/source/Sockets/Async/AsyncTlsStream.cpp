#include <algorithm>
#include <cassert>
#include <cstring>

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

    InitializeBio();
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

  if (Removing || Ssl == nullptr)
    return false;

  HandshakeStarted = true;
  return Drive();
}

bool AsyncTlsStream::AdoptPendingLowerRead()
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  if (Removing || LowerReadClosed || LowerReadPending)
    return false;

  LowerReadPending = true;
  return true;
}

bool AsyncTlsStream::FeedEncryptedInput(const void* data, size_t bytes)
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  if (Removing || Ssl == nullptr)
    return false;

  if (bytes == 0)
    return true;

  if (data == nullptr)
    return false;

  BIO* rbio = SSL_get_rbio(Ssl);
  if (rbio == nullptr)
    return false;

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

      return false;
    }

    offset += size_t(rc);
  }

  if (!HandshakeStarted)
    return true;

  return Drive();
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
    if (!PlainReadPending)
      return true;

    TlsReadClosed = true;
    PlainReadPending = false;
    PlainReadBuffer.reset();
    return QueueReadClosed();

  case Operation::Error:
    LowerReadPending = false;
    LowerReadClosed = true;
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
    return false;

  result = PendingResults.front();
  PendingResults.pop_front();
  return true;
}

bool AsyncTlsStream::HasPendingResult() const
{
  std::lock_guard<std::recursive_mutex> guard(Lock);

  return !PendingResults.empty();
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
  if (Ssl == nullptr || LowerStream == nullptr)
    return false;

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
    return QueueHandshakeCompleted();
  }

  int error = GetSslError(rc);
  if (IsWantIO(error))
    return true;

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
      PlainWriteOffset += size_t(rc);
      if (!DrainEncryptedOutput())
        return false;

      if (size_t(rc) < size)
        return true;

      skip = 0;
      continue;
    }

    int error = GetSslError(rc);
    if (IsWantIO(error))
      return DrainEncryptedOutput();

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

  return QueueError(ConvertSslError(error));
}

bool AsyncTlsStream::DrainEncryptedOutput()
{
  BIO* wbio = SSL_get_wbio(Ssl);
  if (wbio == nullptr)
    return false;

  for (;;)
  {
    size_t pending = size_t(BIO_ctrl_pending(wbio));
    if (pending == 0)
      return true;

    size_t size = std::min(pending, ENCRYPTED_CHUNK_SIZE);
    IO::BufferPtr buffer = std::make_shared<IO::Buffer>();
    if (buffer == nullptr)
      return false;

    buffer->resize(size);

    int rc = BIO_read(wbio, buffer->data(), int(buffer->size()));
    if (rc <= 0)
    {
      if (BIO_should_retry(wbio))
        return true;

      return false;
    }

    buffer->resize(size_t(rc));

    if (!LowerWriter.Push(buffer))
      return false;
  }
}

bool AsyncTlsStream::StartLowerRead()
{
  if (LowerReadPending || LowerReadClosed || Removing)
    return true;

  if (!HandshakeStarted && !HandshakeCompleted)
    return true;

  if (!PlainReadPending && HandshakeCompleted && !ShutdownPending)
    return true;

  IO::BufferPtr buffer = std::make_shared<IO::Buffer>();
  if (buffer == nullptr)
    return false;

  buffer->resize(ENCRYPTED_READ_SIZE);

  if (!LowerStream->StartRead(buffer))
    return false;

  LowerReadPending = true;
  return true;
}

bool AsyncTlsStream::FeedEncryptedInput(IO::BufferPtr buffer, size_t bytes)
{
  if (buffer == nullptr || bytes == 0)
    return true;

  BIO* rbio = SSL_get_rbio(Ssl);
  if (rbio == nullptr)
    return false;

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

      return false;
    }

    offset += size_t(rc);
  }

  return true;
}

bool AsyncTlsStream::CompleteLowerWrite(size_t bytes)
{
  if (!LowerWriter.OnWriteCompleted(bytes))
    return false;

  return CompleteLowerShutdown();
}

bool AsyncTlsStream::CompleteLowerShutdown()
{
  if (!ShutdownCompleted || !LowerWriter.IsIdle())
    return true;

  if (LowerSendShutdownCompleted)
    return true;

  if (LowerStream == nullptr || !LowerStream->ShutdownSend())
    return false;

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
  return true;
}

bool AsyncTlsStream::QueueError(int error)
{
  if (error == 0)
    error = SSL_ERROR_SSL;

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

void AsyncTlsStream::ResetPlainWrite()
{
  PlainWriteBuffers.Clear();
  PlainWriteOffset = 0;
  PlainWriteSize = 0;
  PlainWritePending = false;
}
