#include <Syncme/Sockets/Async/Counter.h>

using namespace Syncme::Sockets::Async;

std::atomic<uint64_t> Syncme::Sockets::Async::AsyncTlsStreams;

std::atomic<uint64_t> Syncme::Sockets::Async::AsyncWriteQueueBytes;
std::atomic<uint64_t> Syncme::Sockets::Async::AsyncWriteQueueBytesPeak;

std::atomic<uint64_t> Syncme::Sockets::Async::AsyncTlsFreeBufferCapacityBytes;
std::atomic<uint64_t> Syncme::Sockets::Async::AsyncTlsFreeBufferCapacityBytesPeak;

std::atomic<uint64_t> Syncme::Sockets::Async::AsyncTlsPendingResultBytes;
std::atomic<uint64_t> Syncme::Sockets::Async::AsyncTlsPendingResultBytesPeak;

std::atomic<uint64_t> Syncme::Sockets::Async::AsyncEnginePendingResultBytes;
std::atomic<uint64_t> Syncme::Sockets::Async::AsyncEnginePendingResultBytesPeak;

std::atomic<uint64_t> Syncme::Sockets::Async::AsyncEnginePendingResultCount;
std::atomic<uint64_t> Syncme::Sockets::Async::AsyncEnginePendingResultCountPeak;

std::atomic<uint64_t> Syncme::Sockets::Async::AsyncReadBufferCapacityBytes;
std::atomic<uint64_t> Syncme::Sockets::Async::AsyncReadBufferCapacityBytesPeak;

std::atomic<uint64_t> Syncme::Sockets::Async::AsyncWriteBufferCapacityBytes;
std::atomic<uint64_t> Syncme::Sockets::Async::AsyncWriteBufferCapacityBytesPeak;

std::atomic<uint64_t> Syncme::Sockets::Async::AsyncTlsLowerReadBufferCapacityBytes;
std::atomic<uint64_t> Syncme::Sockets::Async::AsyncTlsLowerReadBufferCapacityBytesPeak;

std::atomic<uint64_t> Syncme::Sockets::Async::AsyncIoBufferCapacityBytes;
std::atomic<uint64_t> Syncme::Sockets::Async::AsyncIoBufferCapacityBytesPeak;

namespace
{
  void UpdatePeak(
    std::atomic<uint64_t>& peak
    , uint64_t value
  )
  {
    uint64_t current = peak.load(std::memory_order_relaxed);
    while (current < value
      && !peak.compare_exchange_weak(
        current
        , value
        , std::memory_order_relaxed
      ))
    {
    }
  }
}

uint64_t Syncme::Sockets::Async::GetAsyncTlsStreams()
{
  return AsyncTlsStreams.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncWriteQueueBytes()
{
  return AsyncWriteQueueBytes.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncWriteQueueBytesPeak()
{
  return AsyncWriteQueueBytesPeak.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncTlsFreeBufferCapacityBytes()
{
  return AsyncTlsFreeBufferCapacityBytes.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncTlsFreeBufferCapacityBytesPeak()
{
  return AsyncTlsFreeBufferCapacityBytesPeak.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncTlsPendingResultBytes()
{
  return AsyncTlsPendingResultBytes.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncTlsPendingResultBytesPeak()
{
  return AsyncTlsPendingResultBytesPeak.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncEnginePendingResultBytes()
{
  return AsyncEnginePendingResultBytes.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncEnginePendingResultBytesPeak()
{
  return AsyncEnginePendingResultBytesPeak.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncEnginePendingResultCount()
{
  return AsyncEnginePendingResultCount.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncEnginePendingResultCountPeak()
{
  return AsyncEnginePendingResultCountPeak.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncReadBufferCapacityBytes()
{
  return AsyncReadBufferCapacityBytes.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncReadBufferCapacityBytesPeak()
{
  return AsyncReadBufferCapacityBytesPeak.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncWriteBufferCapacityBytes()
{
  return AsyncWriteBufferCapacityBytes.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncWriteBufferCapacityBytesPeak()
{
  return AsyncWriteBufferCapacityBytesPeak.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncTlsLowerReadBufferCapacityBytes()
{
  return AsyncTlsLowerReadBufferCapacityBytes.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncTlsLowerReadBufferCapacityBytesPeak()
{
  return AsyncTlsLowerReadBufferCapacityBytesPeak.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncIoBufferCapacityBytes()
{
  return AsyncIoBufferCapacityBytes.load(std::memory_order_relaxed);
}

uint64_t Syncme::Sockets::Async::GetAsyncIoBufferCapacityBytesPeak()
{
  return AsyncIoBufferCapacityBytesPeak.load(std::memory_order_relaxed);
}

void Syncme::Sockets::Async::TrackAsyncIoBufferAllocation(size_t bytes)
{
  if (bytes == 0)
    return;

  uint64_t value = AsyncIoBufferCapacityBytes.fetch_add(
    uint64_t(bytes)
    , std::memory_order_relaxed
  ) + uint64_t(bytes);

  UpdatePeak(AsyncIoBufferCapacityBytesPeak, value);
}

void Syncme::Sockets::Async::TrackAsyncIoBufferDeallocation(size_t bytes)
{
  if (bytes != 0)
    AsyncIoBufferCapacityBytes.fetch_sub(uint64_t(bytes), std::memory_order_relaxed);
}
