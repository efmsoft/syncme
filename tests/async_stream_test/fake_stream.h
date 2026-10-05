#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <Syncme/Sockets/Async/AsyncStream.h>

namespace AsyncTest
{
  // What one StartWrite() was given, without holding on to any of it: the
  // fake keeps no reference to a buffer, so a test can tell when the code
  // under test has let go of one.
  struct WriteRecord
  {
    std::string Bytes;
    std::vector<const void*> Buffers;
    std::vector<size_t> Offsets;
    std::vector<size_t> Sizes;
  };

  // A lower stream that only records what it is asked to do; the test plays
  // the part of the network and the engine (it completes writes and delivers
  // reads by hand).
  class FakeStream : public Syncme::Sockets::Async::AsyncStream
  {
  public:
    void* Context;
    Syncme::Sockets::IO::BufferPtr PendingRead;
    std::vector<WriteRecord> Writes;
    size_t ReadStarts;
    size_t ShutdownCount;
    bool FailWrites;
    bool Closed;

    explicit FakeStream(void* context = nullptr)
      : Context(context)
      , ReadStarts(0)
      , ShutdownCount(0)
      , FailWrites(false)
      , Closed(false)
    {
    }

    Syncme::Socket* GetSocket() const override
    {
      return nullptr;
    }

    void* GetContext() const override
    {
      return Context;
    }

    bool StartRead(Syncme::Sockets::IO::BufferPtr buffer) override
    {
      PendingRead = std::move(buffer);
      ++ReadStarts;
      return true;
    }

    bool StartWrite(const Syncme::Sockets::Async::BufferChain& buffers) override
    {
      WriteRecord record;
      for (const auto& view : buffers.GetViews())
      {
        record.Bytes.append(view.Buffer->data() + view.Offset, view.Size);
        record.Buffers.push_back(view.Buffer.get());
        record.Offsets.push_back(view.Offset);
        record.Sizes.push_back(view.Size);
      }

      Writes.push_back(std::move(record));
      return !FailWrites;
    }

    bool ShutdownSend() override
    {
      ++ShutdownCount;
      return true;
    }

    void Close() override
    {
      Closed = true;
    }
  };
}
