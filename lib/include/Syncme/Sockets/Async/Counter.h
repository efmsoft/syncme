#pragma once

#include <atomic>
#include <cstddef>
#include <stdint.h>

#include <Syncme/Api.h>

namespace Syncme
{
  namespace Sockets
  {
    namespace Async
    {
      extern std::atomic<uint64_t> AsyncTlsStreams;

      extern std::atomic<uint64_t> AsyncWriteQueueBytes;
      extern std::atomic<uint64_t> AsyncWriteQueueBytesPeak;

      extern std::atomic<uint64_t> AsyncTlsFreeBufferCapacityBytes;
      extern std::atomic<uint64_t> AsyncTlsFreeBufferCapacityBytesPeak;

      extern std::atomic<uint64_t> AsyncTlsPendingResultBytes;
      extern std::atomic<uint64_t> AsyncTlsPendingResultBytesPeak;

      extern std::atomic<uint64_t> AsyncEnginePendingResultBytes;
      extern std::atomic<uint64_t> AsyncEnginePendingResultBytesPeak;

      extern std::atomic<uint64_t> AsyncEnginePendingResultCount;
      extern std::atomic<uint64_t> AsyncEnginePendingResultCountPeak;

      extern std::atomic<uint64_t> AsyncReadBufferCapacityBytes;
      extern std::atomic<uint64_t> AsyncReadBufferCapacityBytesPeak;

      extern std::atomic<uint64_t> AsyncWriteBufferCapacityBytes;
      extern std::atomic<uint64_t> AsyncWriteBufferCapacityBytesPeak;

      extern std::atomic<uint64_t> AsyncTlsLowerReadBufferCapacityBytes;
      extern std::atomic<uint64_t> AsyncTlsLowerReadBufferCapacityBytesPeak;

      extern std::atomic<uint64_t> AsyncIoBufferCapacityBytes;
      extern std::atomic<uint64_t> AsyncIoBufferCapacityBytesPeak;

      SINCMELNK uint64_t GetAsyncTlsStreams();

      SINCMELNK uint64_t GetAsyncWriteQueueBytes();
      SINCMELNK uint64_t GetAsyncWriteQueueBytesPeak();

      SINCMELNK uint64_t GetAsyncTlsFreeBufferCapacityBytes();
      SINCMELNK uint64_t GetAsyncTlsFreeBufferCapacityBytesPeak();

      SINCMELNK uint64_t GetAsyncTlsPendingResultBytes();
      SINCMELNK uint64_t GetAsyncTlsPendingResultBytesPeak();

      SINCMELNK uint64_t GetAsyncEnginePendingResultBytes();
      SINCMELNK uint64_t GetAsyncEnginePendingResultBytesPeak();

      SINCMELNK uint64_t GetAsyncEnginePendingResultCount();
      SINCMELNK uint64_t GetAsyncEnginePendingResultCountPeak();

      SINCMELNK uint64_t GetAsyncReadBufferCapacityBytes();
      SINCMELNK uint64_t GetAsyncReadBufferCapacityBytesPeak();

      SINCMELNK uint64_t GetAsyncWriteBufferCapacityBytes();
      SINCMELNK uint64_t GetAsyncWriteBufferCapacityBytesPeak();

      SINCMELNK uint64_t GetAsyncTlsLowerReadBufferCapacityBytes();
      SINCMELNK uint64_t GetAsyncTlsLowerReadBufferCapacityBytesPeak();

      SINCMELNK uint64_t GetAsyncIoBufferCapacityBytes();
      SINCMELNK uint64_t GetAsyncIoBufferCapacityBytesPeak();

      SINCMELNK void TrackAsyncIoBufferAllocation(size_t bytes);
      SINCMELNK void TrackAsyncIoBufferDeallocation(size_t bytes);
    }
  }
}
