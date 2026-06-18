#ifndef _WIN32

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/uio.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/Async/AsyncStream.h>
#include <Syncme/Sockets/Socket.h>

using namespace Syncme;
using namespace Syncme::Sockets::Async;
namespace IO = Syncme::Sockets::IO;

namespace
{
  constexpr size_t MAX_IOV = 16;

  class LinuxAsyncEngine;

  class LinuxAsyncStream : public AsyncStream
  {
    friend class LinuxAsyncEngine;

    LinuxAsyncEngine* Engine;
    Socket* Skt;
    void* Context;

    bool ReadPending;
    bool WritePending;
    bool Removing;
    bool ReadClosed;

    IO::BufferPtr ReadBuffer;
    BufferChain WriteBuffers;
    size_t WriteOffset;
    size_t WriteSize;

  public:
    LinuxAsyncStream(
      LinuxAsyncEngine* engine
      , Socket* socket
      , void* context
    )
      : Engine(engine)
      , Skt(socket)
      , Context(context)
      , ReadPending(false)
      , WritePending(false)
      , Removing(false)
      , ReadClosed(false)
      , WriteOffset(0)
      , WriteSize(0)
    {
    }

    Socket* GetSocket() const override
    {
      return Skt;
    }

    void* GetContext() const override
    {
      return Context;
    }

    bool StartRead(IO::BufferPtr buffer) override;
    bool StartWrite(const BufferChain& buffers) override;
    bool ShutdownSend() override;
    void Close() override;
  };

  using LinuxAsyncStreamPtr = std::shared_ptr<LinuxAsyncStream>;

  class LinuxAsyncEngine : public AsyncEngine
  {
    int Poll;
    int StopEvent;
    std::atomic<bool> Stopping;

    std::mutex Lock;
    std::unordered_map<int, LinuxAsyncStreamPtr> Entries;
    std::deque<Result> PendingResults;
    std::vector<epoll_event> Events;

  public:
    LinuxAsyncEngine()
      : Poll(-1)
      , StopEvent(-1)
      , Stopping(false)
      , Events(64)
    {
      Poll = epoll_create(1);
      if (Poll == -1)
      {
        LogosE("epoll_create failed");
        return;
      }

      StopEvent = eventfd(0, EFD_NONBLOCK);
      if (StopEvent == -1)
      {
        LogosE("eventfd failed");
        return;
      }

      epoll_event ev{};
      ev.data.fd = StopEvent;
      ev.events = EPOLLIN;

      if (epoll_ctl(Poll, EPOLL_CTL_ADD, StopEvent, &ev) == -1)
      {
        LogosE("epoll_ctl(EPOLL_CTL_ADD) failed for StopEvent");
      }
    }

    ~LinuxAsyncEngine() override
    {
      if (StopEvent != -1 && Poll != -1)
        epoll_ctl(Poll, EPOLL_CTL_DEL, StopEvent, nullptr);

      if (Poll != -1)
      {
        close(Poll);
        Poll = -1;
      }

      if (StopEvent != -1)
      {
        close(StopEvent);
        StopEvent = -1;
      }
    }

    bool Add(
      Socket* socket
      , void* context
      , AsyncStreamPtr& stream
    ) override
    {
      stream.reset();

      if (Poll == -1 || socket == nullptr || !socket->IsAttached())
        return false;

      int fd = socket->Handle;
      if (fd == -1)
        return false;

      SetNonBlocking(fd);

      auto item = std::make_shared<LinuxAsyncStream>(this, socket, context);

      epoll_event ev = MakeEvent(item.get());
      ev.data.fd = fd;

      std::lock_guard<std::mutex> guard(Lock);
      if (Entries.find(fd) != Entries.end())
        return false;

      if (epoll_ctl(Poll, EPOLL_CTL_ADD, fd, &ev) == -1)
      {
        LogosE("epoll_ctl(EPOLL_CTL_ADD) failed");
        return false;
      }

      Entries[fd] = item;
      stream = item;
      return true;
    }

    bool Remove(AsyncStream* stream) override
    {
      if (stream == nullptr)
        return false;

      auto* item = static_cast<LinuxAsyncStream*>(stream);
      Socket* socket = item->GetSocket();
      if (socket == nullptr)
        return false;

      int fd = socket->Handle;
      if (fd == -1)
        return false;

      std::lock_guard<std::mutex> guard(Lock);
      auto it = Entries.find(fd);
      if (it == Entries.end())
        return false;

      it->second->Removing = true;
      epoll_ctl(Poll, EPOLL_CTL_DEL, fd, nullptr);
      Entries.erase(it);
      return true;
    }

    bool Wait(Result& result, int timeout) override
    {
      result = Result();

      if (PopPendingResult(result))
        return true;

      for (;;)
      {
        int n = epoll_wait(Poll, Events.data(), int(Events.size()), timeout);
        if (n < 0 && errno == EINTR)
          continue;

        if (n < 0)
        {
          result.Error = errno;
          LogosE("epoll_wait failed");
          return false;
        }

        if (n == 0)
          return true;

        for (int i = 0; i < n; ++i)
          ProcessEpollEvent(Events[i]);

        if (PopPendingResult(result))
          return true;
      }
    }

    void Wake() override
    {
      SignalStopEvent();
    }

    void Stop() override
    {
      Stopping.store(true);
      SignalStopEvent();
    }

    bool StartRead(
      LinuxAsyncStream* stream
      , IO::BufferPtr buffer
    )
    {
      if (stream == nullptr || buffer == nullptr || buffer->empty())
        return false;

      std::lock_guard<std::mutex> guard(Lock);

      if (stream->Removing || stream->ReadPending || stream->ReadClosed)
        return false;

      stream->ReadBuffer = buffer;
      stream->ReadPending = true;

      bool ok = TryReadLocked(stream);
      UpdateInterestLocked(stream);
      return ok;
    }

    bool StartWrite(
      LinuxAsyncStream* stream
      , const BufferChain& buffers
    )
    {
      if (stream == nullptr || buffers.IsEmpty())
        return false;

      std::lock_guard<std::mutex> guard(Lock);

      if (stream->Removing || stream->WritePending)
        return false;

      stream->WriteBuffers = buffers;
      stream->WriteOffset = 0;
      stream->WriteSize = buffers.Size();
      stream->WritePending = true;

      bool ok = TryWriteLocked(stream);
      UpdateInterestLocked(stream);
      return ok;
    }

  private:
    static void SetNonBlocking(int fd)
    {
      int flags = fcntl(fd, F_GETFL, 0);
      if (flags == -1)
        return;

      fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    void SignalStopEvent()
    {
      if (StopEvent == -1)
        return;

      uint64_t value = 1;
      write(StopEvent, &value, sizeof(value));
    }

    bool PopPendingResult(Result& result)
    {
      std::lock_guard<std::mutex> guard(Lock);
      if (PendingResults.empty())
        return false;

      result = PendingResults.front();
      PendingResults.pop_front();
      return true;
    }

    void QueueResultLocked(
      const LinuxAsyncStreamPtr& stream
      , Operation op
      , IO::BufferPtr buffer
      , size_t bytes
      , int error
    )
    {
      if (stream == nullptr || stream->Removing)
        return;

      Result result;
      result.Stream = stream;
      result.Context = stream->Context;
      result.Op = op;
      result.Buffer = buffer;
      result.Bytes = bytes;
      result.Error = error;
      PendingResults.push_back(result);
    }

    void ProcessEpollEvent(const epoll_event& ev)
    {
      if (ev.data.fd == StopEvent)
      {
        uint64_t value = 0;
        read(StopEvent, &value, sizeof(value));

        Result result;
        result.Op = Stopping.load() ? Operation::Stop : Operation::Wake;

        std::lock_guard<std::mutex> guard(Lock);
        PendingResults.push_back(result);
        return;
      }

      std::lock_guard<std::mutex> guard(Lock);

      auto it = Entries.find(ev.data.fd);
      if (it == Entries.end())
        return;

      LinuxAsyncStreamPtr stream = it->second;
      if (stream->Removing)
        return;

      if (ev.events & EPOLLERR)
      {
        QueueResultLocked(
          stream
          , Operation::Error
          , nullptr
          , 0
          , GetSocketError(stream->Skt->Handle)
        );
        return;
      }

      if (stream->ReadPending && (ev.events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)))
        TryReadLocked(stream.get());

      if (stream->WritePending && (ev.events & EPOLLOUT))
        TryWriteLocked(stream.get());

      if ((ev.events & EPOLLHUP) && !stream->ReadPending && !stream->WritePending)
        QueueResultLocked(stream, Operation::ReadClosed, nullptr, 0, 0);

      UpdateInterestLocked(stream.get());
    }

    static int GetSocketError(int fd)
    {
      int error = 0;
      socklen_t size = sizeof(error);

      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == -1)
        return errno;

      if (error == 0)
        return EPIPE;

      return error;
    }

    bool TryReadLocked(LinuxAsyncStream* stream)
    {
      if (stream == nullptr || !stream->ReadPending || stream->ReadBuffer == nullptr)
        return true;

      LinuxAsyncStreamPtr owner = FindStreamLocked(stream);
      if (owner == nullptr)
        return false;

      ssize_t n = recv(
        stream->Skt->Handle
        , stream->ReadBuffer->data()
        , stream->ReadBuffer->size()
        , 0
      );

      if (n > 0)
      {
        IO::BufferPtr buffer = stream->ReadBuffer;
        buffer->resize(size_t(n));

        stream->ReadPending = false;
        stream->ReadBuffer.reset();

        QueueResultLocked(owner, Operation::Read, buffer, size_t(n), 0);
        return true;
      }

      if (n == 0)
      {
        stream->ReadPending = false;
        stream->ReadClosed = true;
        stream->ReadBuffer.reset();
        stream->Skt->Peer.Disconnected = true;

        QueueResultLocked(owner, Operation::ReadClosed, nullptr, 0, 0);
        return true;
      }

      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return true;

      stream->ReadPending = false;
      stream->ReadBuffer.reset();
      QueueResultLocked(owner, Operation::Error, nullptr, 0, errno);
      return false;
    }

    bool TryWriteLocked(LinuxAsyncStream* stream)
    {
      if (stream == nullptr || !stream->WritePending)
        return true;

      LinuxAsyncStreamPtr owner = FindStreamLocked(stream);
      if (owner == nullptr)
        return false;

      for (;;)
      {
        iovec buffers[MAX_IOV]{};
        int count = BuildIov(stream, buffers, MAX_IOV);
        if (count == 0)
        {
          CompleteWriteLocked(owner);
          return true;
        }

        ssize_t n = writev(stream->Skt->Handle, buffers, count);
        if (n > 0)
        {
          stream->WriteOffset += size_t(n);
          if (stream->WriteOffset >= stream->WriteSize)
          {
            CompleteWriteLocked(owner);
            return true;
          }

          continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
          return true;

        int error = n == 0 ? EPIPE : errno;
        stream->WritePending = false;
        stream->WriteBuffers.Clear();
        stream->WriteOffset = 0;
        stream->WriteSize = 0;
        QueueResultLocked(owner, Operation::Error, nullptr, 0, error);
        return false;
      }
    }

    int BuildIov(
      LinuxAsyncStream* stream
      , iovec* buffers
      , size_t capacity
    ) const
    {
      if (stream == nullptr || buffers == nullptr || capacity == 0)
        return 0;

      size_t skip = stream->WriteOffset;
      int count = 0;

      for (const auto& view : stream->WriteBuffers.GetViews())
      {
        if (skip >= view.Size)
        {
          skip -= view.Size;
          continue;
        }

        buffers[count].iov_base = view.Buffer->data() + view.Offset + skip;
        buffers[count].iov_len = view.Size - skip;
        skip = 0;

        if (++count == int(capacity))
          break;
      }

      return count;
    }

    void CompleteWriteLocked(const LinuxAsyncStreamPtr& stream)
    {
      size_t bytes = stream->WriteSize;

      stream->WritePending = false;
      stream->WriteBuffers.Clear();
      stream->WriteOffset = 0;
      stream->WriteSize = 0;

      QueueResultLocked(stream, Operation::Write, nullptr, bytes, 0);
    }

    LinuxAsyncStreamPtr FindStreamLocked(LinuxAsyncStream* stream) const
    {
      if (stream == nullptr || stream->Skt == nullptr)
        return LinuxAsyncStreamPtr();

      auto it = Entries.find(stream->Skt->Handle);
      if (it == Entries.end())
        return LinuxAsyncStreamPtr();

      return it->second;
    }

    void UpdateInterestLocked(LinuxAsyncStream* stream)
    {
      if (stream == nullptr || stream->Skt == nullptr || stream->Removing)
        return;

      epoll_event ev = MakeEvent(stream);
      ev.data.fd = stream->Skt->Handle;

      if (epoll_ctl(Poll, EPOLL_CTL_MOD, stream->Skt->Handle, &ev) == -1)
      {
        LogosE("epoll_ctl(EPOLL_CTL_MOD) failed");
      }
    }

    static epoll_event MakeEvent(LinuxAsyncStream* stream)
    {
      epoll_event ev{};
      ev.events = EPOLLRDHUP | EPOLLERR | EPOLLHUP;

      if (stream != nullptr)
      {
        if (stream->ReadPending)
          ev.events |= EPOLLIN;

        if (stream->WritePending)
          ev.events |= EPOLLOUT;
      }

      return ev;
    }
  };

  bool LinuxAsyncStream::StartRead(IO::BufferPtr buffer)
  {
    return Engine != nullptr && Engine->StartRead(this, buffer);
  }

  bool LinuxAsyncStream::StartWrite(const BufferChain& buffers)
  {
    return Engine != nullptr && Engine->StartWrite(this, buffers);
  }

  bool LinuxAsyncStream::ShutdownSend()
  {
    if (Skt == nullptr || Skt->Handle == -1)
      return false;

    return shutdown(Skt->Handle, SD_SEND) == 0;
  }

  void LinuxAsyncStream::Close()
  {
    if (Skt != nullptr)
      Skt->Close();
  }
}

std::unique_ptr<AsyncEngine> AsyncEngine::Create()
{
  return std::make_unique<LinuxAsyncEngine>();
}

#endif
