#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <winsock2.h>
#include <windows.h>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sockets/Async/AsyncStream.h>
#include <Syncme/Sockets/Socket.h>

using namespace Syncme;
using namespace Syncme::Sockets::Async;
namespace IO = Syncme::Sockets::IO;

namespace
{
  constexpr size_t MAX_WSABUF = 16;

  enum class IocpOperationType
  {
    Read,
    Write
  };

  class WindowsAsyncEngine;
  class WindowsAsyncStream;

  struct IocpOperation
  {
    OVERLAPPED Overlapped;
    WindowsAsyncStream* Stream;
    IocpOperationType Type;
    bool Pending;

    IocpOperation(
      WindowsAsyncStream* stream
      , IocpOperationType type
    )
      : Overlapped{}
      , Stream(stream)
      , Type(type)
      , Pending(false)
    {
    }
  };

  class WindowsAsyncStream : public AsyncStream
    , public std::enable_shared_from_this<WindowsAsyncStream>
  {
    friend class WindowsAsyncEngine;

    WindowsAsyncEngine* Engine;
    Socket* Skt;
    void* Context;

    bool Removing;
    bool ReadClosed;
    size_t PendingCount;

    IO::BufferPtr ReadBuffer;
    DWORD ReadFlags;

    BufferChain WriteBuffers;
    size_t WriteOffset;
    size_t WriteSize;

    IocpOperation ReadOp;
    IocpOperation WriteOp;

  public:
    WindowsAsyncStream(
      WindowsAsyncEngine* engine
      , Socket* socket
      , void* context
    )
      : Engine(engine)
      , Skt(socket)
      , Context(context)
      , Removing(false)
      , ReadClosed(false)
      , PendingCount(0)
      , ReadFlags(0)
      , WriteOffset(0)
      , WriteSize(0)
      , ReadOp(this, IocpOperationType::Read)
      , WriteOp(this, IocpOperationType::Write)
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

  using WindowsAsyncStreamPtr = std::shared_ptr<WindowsAsyncStream>;

  class WindowsAsyncEngine : public AsyncEngine
  {
    static constexpr ULONG_PTR WAKE_KEY = 1;
    static constexpr ULONG_PTR STOP_KEY = 2;

    HANDLE Port;
    std::atomic<bool> Stopping;

    std::mutex Lock;
    std::unordered_map<Socket*, WindowsAsyncStreamPtr> Entries;
    std::vector<WindowsAsyncStreamPtr> Retired;
    std::deque<Result> PendingResults;

  public:
    WindowsAsyncEngine()
      : Port(nullptr)
      , Stopping(false)
    {
      Port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
      if (Port == nullptr)
      {
        LogosE("CreateIoCompletionPort failed");
      }
    }

    ~WindowsAsyncEngine() override
    {
      ShutdownAll();

      if (Port != nullptr)
      {
        CloseHandle(Port);
        Port = nullptr;
      }
    }

    bool Add(
      Socket* socket
      , void* context
      , AsyncStreamPtr& stream
    ) override
    {
      stream.reset();

      if (Port == nullptr || socket == nullptr || !socket->IsAttached())
        return false;

      if (socket->Handle == -1)
        return false;

      std::lock_guard<std::mutex> guard(Lock);

      if (Entries.find(socket) != Entries.end())
        return false;

      HANDLE handle = reinterpret_cast<HANDLE>(
        static_cast<intptr_t>(socket->Handle)
      );

      HANDLE rc = CreateIoCompletionPort(handle, Port, 0, 0);
      if (rc != Port)
      {
        LogosE("CreateIoCompletionPort failed for socket");
        return false;
      }

      WindowsAsyncStreamPtr item = std::make_shared<WindowsAsyncStream>(
        this
        , socket
        , context
      );

      Entries[socket] = item;
      stream = item;
      return true;
    }

    bool Remove(AsyncStream* stream) override
    {
      if (stream == nullptr)
        return false;

      auto* item = static_cast<WindowsAsyncStream*>(stream);
      Socket* socket = item->GetSocket();
      if (socket == nullptr)
        return false;

      std::lock_guard<std::mutex> guard(Lock);

      auto it = Entries.find(socket);
      if (it == Entries.end())
        return false;

      RetireLocked(it);
      CleanupRetiredLocked();
      return true;
    }

    bool Wait(Result& result, int timeout) override
    {
      result = Result();

      if (PopPendingResult(result))
        return true;

      for (;;)
      {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = nullptr;

        BOOL ok = GetQueuedCompletionStatus(
          Port
          , &bytes
          , &key
          , &overlapped
          , timeout < 0 ? INFINITE : DWORD(timeout)
        );

        if (overlapped == nullptr)
        {
          if (ok)
          {
            result.Op = key == WAKE_KEY && !Stopping.load()
              ? Operation::Wake
              : Operation::Stop;

            return result.Op != Operation::Stop;
          }

          DWORD error = GetLastError();
          result.Error = int(error);

          if (error == WAIT_TIMEOUT)
            return true;

          LogosE("GetQueuedCompletionStatus failed");
          return false;
        }

        ProcessCompletion(ok, bytes, overlapped);

        if (PopPendingResult(result))
          return true;
      }
    }

    void Wake() override
    {
      if (Port != nullptr)
        PostQueuedCompletionStatus(Port, 0, WAKE_KEY, nullptr);
    }

    void Stop() override
    {
      Stopping.store(true);

      if (Port != nullptr)
        PostQueuedCompletionStatus(Port, 0, STOP_KEY, nullptr);
    }

    bool StartRead(
      WindowsAsyncStream* stream
      , IO::BufferPtr buffer
    )
    {
      if (stream == nullptr || buffer == nullptr || buffer->empty())
        return false;

      std::lock_guard<std::mutex> guard(Lock);

      if (stream->Removing || stream->ReadOp.Pending || stream->ReadClosed)
        return false;

      stream->ReadBuffer = buffer;
      return PostReadLocked(stream);
    }

    bool StartWrite(
      WindowsAsyncStream* stream
      , const BufferChain& buffers
    )
    {
      if (stream == nullptr || buffers.IsEmpty())
        return false;

      std::lock_guard<std::mutex> guard(Lock);

      if (stream->Removing || stream->WriteOp.Pending || stream->WriteSize != 0)
        return false;

      stream->WriteBuffers = buffers;
      stream->WriteOffset = 0;
      stream->WriteSize = buffers.Size();
      return PostWriteLocked(stream);
    }

  private:
    using EntryIterator = std::unordered_map<Socket*, WindowsAsyncStreamPtr>::iterator;

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
      const WindowsAsyncStreamPtr& stream
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

    void ProcessCompletion(
      BOOL ok
      , DWORD bytes
      , OVERLAPPED* overlapped
    )
    {
      auto* op = reinterpret_cast<IocpOperation*>(overlapped);
      if (op == nullptr || op->Stream == nullptr)
        return;

      std::lock_guard<std::mutex> guard(Lock);

      WindowsAsyncStream* stream = op->Stream;
      WindowsAsyncStreamPtr owner = FindStreamLocked(stream);
      if (owner == nullptr)
      {
        owner = FindRetiredLocked(stream);
        if (owner == nullptr)
          return;
      }

      op->Pending = false;
      if (stream->PendingCount != 0)
        --stream->PendingCount;

      if (stream->Removing)
      {
        CleanupRetiredLocked();
        return;
      }

      if (!ok)
      {
        DWORD error = GetLastError();
        if (error != ERROR_OPERATION_ABORTED)
          QueueResultLocked(owner, Operation::Error, nullptr, 0, int(error));

        CleanupRetiredLocked();
        return;
      }

      if (op->Type == IocpOperationType::Read)
        CompleteReadLocked(owner, bytes);
      else
        CompleteWriteLocked(owner, bytes);

      CleanupRetiredLocked();
    }

    void CompleteReadLocked(
      const WindowsAsyncStreamPtr& stream
      , DWORD bytes
    )
    {
      IO::BufferPtr buffer = stream->ReadBuffer;
      stream->ReadBuffer.reset();

      if (bytes == 0)
      {
        stream->ReadClosed = true;
        stream->Skt->Peer.Disconnected = true;
        QueueResultLocked(stream, Operation::ReadClosed, nullptr, 0, 0);
        return;
      }

      if (buffer != nullptr)
      {
        buffer->resize(size_t(bytes));
        QueueResultLocked(stream, Operation::Read, buffer, size_t(bytes), 0);
      }
    }

    void CompleteWriteLocked(
      const WindowsAsyncStreamPtr& stream
      , DWORD bytes
    )
    {
      if (bytes == 0 && stream->WriteSize != 0)
      {
        stream->WriteBuffers.Clear();
        stream->WriteOffset = 0;
        stream->WriteSize = 0;
        QueueResultLocked(stream, Operation::Error, nullptr, 0, WSAECONNRESET);
        return;
      }

      stream->WriteOffset += size_t(bytes);

      if (stream->WriteOffset < stream->WriteSize)
      {
        PostWriteLocked(stream.get());
        return;
      }

      size_t total = stream->WriteSize;
      stream->WriteBuffers.Clear();
      stream->WriteOffset = 0;
      stream->WriteSize = 0;

      QueueResultLocked(stream, Operation::Write, nullptr, total, 0);
    }

    bool PostReadLocked(WindowsAsyncStream* stream)
    {
      if (stream == nullptr || stream->ReadOp.Pending || stream->ReadBuffer == nullptr)
        return true;

      memset(&stream->ReadOp.Overlapped, 0, sizeof(stream->ReadOp.Overlapped));

      WSABUF buffer{};
      buffer.buf = stream->ReadBuffer->data();
      buffer.len = ULONG(stream->ReadBuffer->size());

      stream->ReadFlags = 0;
      DWORD bytes = 0;

      stream->ReadOp.Pending = true;
      ++stream->PendingCount;

      int rc = WSARecv(
        SOCKET(stream->Skt->Handle)
        , &buffer
        , 1
        , &bytes
        , &stream->ReadFlags
        , &stream->ReadOp.Overlapped
        , nullptr
      );

      if (rc == SOCKET_ERROR)
      {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
          stream->ReadOp.Pending = false;
          if (stream->PendingCount != 0)
            --stream->PendingCount;

          stream->ReadBuffer.reset();
          QueueResultLocked(
            stream->shared_from_this()
            , Operation::Error
            , nullptr
            , 0
            , error
          );
          return true;
        }
      }

      return true;
    }

    bool PostWriteLocked(WindowsAsyncStream* stream)
    {
      if (stream == nullptr || stream->WriteOp.Pending)
        return true;

      WSABUF buffers[MAX_WSABUF]{};
      DWORD count = BuildWsaBuffers(stream, buffers, MAX_WSABUF);
      if (count == 0)
        return false;

      memset(&stream->WriteOp.Overlapped, 0, sizeof(stream->WriteOp.Overlapped));

      stream->WriteOp.Pending = true;
      ++stream->PendingCount;

      DWORD bytes = 0;
      int rc = WSASend(
        SOCKET(stream->Skt->Handle)
        , buffers
        , count
        , &bytes
        , 0
        , &stream->WriteOp.Overlapped
        , nullptr
      );

      if (rc == SOCKET_ERROR)
      {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
          stream->WriteOp.Pending = false;
          if (stream->PendingCount != 0)
            --stream->PendingCount;

          stream->WriteBuffers.Clear();
          stream->WriteOffset = 0;
          stream->WriteSize = 0;
          QueueResultLocked(
            stream->shared_from_this()
            , Operation::Error
            , nullptr
            , 0
            , error
          );
          return true;
        }
      }

      return true;
    }

    static DWORD BuildWsaBuffers(
      WindowsAsyncStream* stream
      , WSABUF* buffers
      , size_t capacity
    )
    {
      if (stream == nullptr || buffers == nullptr || capacity == 0)
        return 0;

      size_t skip = stream->WriteOffset;
      DWORD count = 0;

      for (const auto& view : stream->WriteBuffers.GetViews())
      {
        if (skip >= view.Size)
        {
          skip -= view.Size;
          continue;
        }

        buffers[count].buf = view.Buffer->data() + view.Offset + skip;
        buffers[count].len = ULONG(view.Size - skip);
        skip = 0;

        if (++count == DWORD(capacity))
          break;
      }

      return count;
    }

    WindowsAsyncStreamPtr FindStreamLocked(WindowsAsyncStream* stream) const
    {
      if (stream == nullptr || stream->Skt == nullptr)
        return WindowsAsyncStreamPtr();

      auto it = Entries.find(stream->Skt);
      if (it == Entries.end())
        return WindowsAsyncStreamPtr();

      return it->second;
    }

    WindowsAsyncStreamPtr FindRetiredLocked(WindowsAsyncStream* stream) const
    {
      auto it = std::find_if(
        Retired.begin()
        , Retired.end()
        , [stream](const WindowsAsyncStreamPtr& item) {
          return item.get() == stream;
        }
      );

      if (it == Retired.end())
        return WindowsAsyncStreamPtr();

      return *it;
    }

    void RetireLocked(EntryIterator it)
    {
      WindowsAsyncStreamPtr stream = it->second;
      stream->Removing = true;

      if (stream->Skt != nullptr && stream->Skt->Handle != -1)
      {
        CancelIoEx(
          reinterpret_cast<HANDLE>(static_cast<intptr_t>(stream->Skt->Handle))
          , nullptr
        );
      }

      Retired.push_back(stream);
      Entries.erase(it);
    }

    void ShutdownAll()
    {
      {
        std::lock_guard<std::mutex> guard(Lock);

        while (!Entries.empty())
          RetireLocked(Entries.begin());

        CleanupRetiredLocked();
      }

      DrainRetired();

      std::lock_guard<std::mutex> guard(Lock);
      CleanupRetiredLocked();
      PendingResults.clear();
    }

    void DrainRetired()
    {
      while (HasPendingRetired())
      {
        Result result;
        Wait(result, 100);
      }
    }

    bool HasPendingRetired()
    {
      std::lock_guard<std::mutex> guard(Lock);

      for (const auto& stream : Retired)
      {
        if (stream->PendingCount != 0)
          return true;
      }

      return false;
    }

    void CleanupRetiredLocked()
    {
      for (auto it = Retired.begin(); it != Retired.end();)
      {
        if ((*it)->PendingCount == 0)
          it = Retired.erase(it);
        else
          ++it;
      }
    }
  };

  bool WindowsAsyncStream::StartRead(IO::BufferPtr buffer)
  {
    return Engine != nullptr && Engine->StartRead(this, buffer);
  }

  bool WindowsAsyncStream::StartWrite(const BufferChain& buffers)
  {
    return Engine != nullptr && Engine->StartWrite(this, buffers);
  }

  bool WindowsAsyncStream::ShutdownSend()
  {
    if (Skt == nullptr || Skt->Handle == -1)
      return false;

    return shutdown(SOCKET(Skt->Handle), SD_SEND) == 0;
  }

  void WindowsAsyncStream::Close()
  {
    if (Skt != nullptr)
      Skt->Close();
  }
}

std::unique_ptr<AsyncEngine> AsyncEngine::Create()
{
  return std::make_unique<WindowsAsyncEngine>();
}

#endif
