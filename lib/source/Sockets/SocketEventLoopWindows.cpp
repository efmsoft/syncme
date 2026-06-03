#ifdef _WIN32

#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>
#include <winsock2.h>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/SocketEventLoop.h>
#include <Syncme/TickCount.h>

using namespace Syncme;

namespace
{
  enum class OperationType
  {
    Read,
    Write
  };

  struct IocpState;

  struct IocpOperation
  {
    OVERLAPPED Overlapped;
    IocpState* State;
    OperationType Type;
    bool Posted;

    IocpOperation(IocpState* state, OperationType type)
      : Overlapped{}
      , State(state)
      , Type(type)
      , Posted(false)
    {
    }
  };

  struct IocpState
  {
    Socket* Skt;
    void* Context;
    int Events;
    bool ReadPending;
    bool WritePending;
    bool WriteNotified;
    bool Removing;
    size_t PendingCount;
    char ReadByte;
    IocpOperation ReadOp;
    IocpOperation WriteOp;

    IocpState(Socket* socket, void* context, int events)
      : Skt(socket)
      , Context(context)
      , Events(events)
      , ReadPending(false)
      , WritePending(false)
      , WriteNotified(false)
      , Removing(false)
      , PendingCount(0)
      , ReadByte(0)
      , ReadOp(this, OperationType::Read)
      , WriteOp(this, OperationType::Write)
    {
    }
  };

  class WindowsSocketEventLoop : public SocketEventLoop
  {
    static constexpr ULONG_PTR WAKE_KEY = 1;
    static constexpr ULONG_PTR STOP_KEY = 2;

    HANDLE Port;
    std::atomic<bool> Stopping;
    std::unordered_map<Socket*, std::unique_ptr<IocpState>> Entries;
    std::vector<std::unique_ptr<IocpState>> Retired;

  public:
    WindowsSocketEventLoop()
      : Port(nullptr)
      , Stopping(false)
    {
      Port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
      if (Port == nullptr)
      {
        LogosE("CreateIoCompletionPort failed");
      }
    }

    ~WindowsSocketEventLoop() override
    {
      ShutdownAll();

      if (Port)
      {
        CloseHandle(Port);
        Port = nullptr;
      }
    }

    bool Add(Socket* socket, void* context, int events) override
    {
      if (Port == nullptr || socket == nullptr)
        return false;

      if (Entries.count(socket))
        return false;

      HANDLE handle = reinterpret_cast<HANDLE>(socket->GetFD());
      HANDLE rc = CreateIoCompletionPort(handle, Port, 0, 0);
      if (rc != Port)
      {
        LogosE("CreateIoCompletionPort failed for socket");
        return false;
      }

      auto state = std::make_unique<IocpState>(socket, context, events);
      IocpState* rawState = state.get();
      Entries[socket] = std::move(state);

      return Arm(rawState);
    }

    bool Update(Socket* socket, int events) override
    {
      auto it = Entries.find(socket);
      if (it == Entries.end())
        return false;

      IocpState* state = it->second.get();
      if ((state->Events & EVENT_WRITE) && !(events & EVENT_WRITE))
      {
        state->WriteNotified = false;
      }

      state->Events = events;
      return Arm(state);
    }

    bool Remove(Socket* socket) override
    {
      auto it = Entries.find(socket);
      if (it == Entries.end())
        return false;

      Retire(it);
      CleanupRetired();
      return true;
    }

    bool Wait(SocketEventLoopResult& result, int timeout) override
    {
      result = SocketEventLoopResult();

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
            if (key == WAKE_KEY && !Stopping.load())
            {
              result.Operation = SocketEventLoopOperation::Wake;
              return true;
            }

            result.Operation = SocketEventLoopOperation::Stop;
            return false;
          }

          DWORD error = GetLastError();
          result.Error = int(error);

          if (error == WAIT_TIMEOUT)
            return true;

          LogosE("GetQueuedCompletionStatus failed");
          return false;
        }

        IocpOperation* op = reinterpret_cast<IocpOperation*>(overlapped);
        IocpState* state = op->State;
        if (state == nullptr)
          continue;

        op->Posted = false;

        if (state->PendingCount)
          --state->PendingCount;

        int event = EVENT_CLOSE;
        SocketEventLoopOperation operation = SocketEventLoopOperation::Close;

        if (op->Type == OperationType::Read)
        {
          state->ReadPending = false;
          event = EVENT_READ;
          operation = SocketEventLoopOperation::Read;
        }
        else
        {
          state->WritePending = false;
          state->WriteNotified = true;
          event = EVENT_WRITE;
          operation = SocketEventLoopOperation::Write;
        }

        bool removed = state->Removing || !Entries.count(state->Skt);

        if (!ok)
        {
          DWORD error = GetLastError();
          result.Error = int(error);

          if (error == ERROR_OPERATION_ABORTED)
          {
            CleanupRetired();
            continue;
          }

          event = EVENT_CLOSE;
          operation = SocketEventLoopOperation::Close;
        }

        if (removed)
        {
          CleanupRetired();
          continue;
        }

        if (event == EVENT_CLOSE)
        {
          state->Skt->Peer.Disconnected = true;
          state->Skt->Peer.When = GetTimeInMillisec();
        }

        result.Skt = state->Skt;
        result.Context = state->Context;
        result.Events = event;
        result.Operation = operation;
        result.Bytes = size_t(bytes);

        CleanupRetired();
        return true;
      }
    }

    void Wake() override
    {
      if (Port)
        PostQueuedCompletionStatus(Port, 0, WAKE_KEY, nullptr);
    }

    void Stop() override
    {
      Stopping.store(true);

      if (Port)
        PostQueuedCompletionStatus(Port, 0, STOP_KEY, nullptr);
    }

  private:
    using EntryIterator = std::unordered_map<Socket*, std::unique_ptr<IocpState>>::iterator;

    void Retire(EntryIterator it)
    {
      IocpState* state = it->second.get();
      state->Removing = true;

      CancelIoEx(
        reinterpret_cast<HANDLE>(state->Skt->GetFD())
        , nullptr
      );

      Retired.push_back(std::move(it->second));
      Entries.erase(it);
    }

    void ShutdownAll()
    {
      while (Entries.empty() == false)
      {
        Retire(Entries.begin());
      }

      DrainRetired();
      Retired.clear();
    }

    void DrainRetired()
    {
      while (HasPendingRetired())
      {
        SocketEventLoopResult result;
        Wait(result, 100);
        CleanupRetired();
      }
    }

    bool HasPendingRetired() const
    {
      for (const auto& state : Retired)
      {
        if (state->PendingCount != 0)
          return true;
      }

      return false;
    }

    void CleanupRetired()
    {
      for (auto it = Retired.begin(); it != Retired.end();)
      {
        if ((*it)->PendingCount == 0)
          it = Retired.erase(it);
        else
          ++it;
      }
    }

    bool Arm(IocpState* state)
    {
      if (state == nullptr || state->Removing)
        return false;

      if ((state->Events & EVENT_READ) && !state->ReadPending)
      {
        if (!PostRead(state))
          return false;
      }

      if ((state->Events & EVENT_WRITE) && !state->WritePending && !state->WriteNotified)
      {
        if (!PostWrite(state))
          return false;
      }

      return true;
    }

    bool PostRead(IocpState* state)
    {
      if (state->ReadPending)
        return true;

      memset(&state->ReadOp.Overlapped, 0, sizeof(state->ReadOp.Overlapped));

      WSABUF buffer{};
      buffer.buf = &state->ReadByte;
      buffer.len = 0;

      DWORD flags = 0;
      DWORD bytes = 0;
      int rc = WSARecv(
        SOCKET(state->Skt->GetFD())
        , &buffer
        , 1
        , &bytes
        , &flags
        , &state->ReadOp.Overlapped
        , nullptr
      );

      if (rc == SOCKET_ERROR)
      {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
          LogosE("WSARecv failed");
          return false;
        }
      }

      state->ReadPending = true;
      state->ReadOp.Posted = true;
      ++state->PendingCount;
      return true;
    }

    bool PostWrite(IocpState* state)
    {
      if (state->WritePending || state->WriteNotified)
        return true;

      memset(&state->WriteOp.Overlapped, 0, sizeof(state->WriteOp.Overlapped));

      if (!PostQueuedCompletionStatus(
        Port
        , 0
        , 0
        , &state->WriteOp.Overlapped
      ))
      {
        LogosE("PostQueuedCompletionStatus failed for write event");
        return false;
      }

      state->WritePending = true;
      state->WriteOp.Posted = true;
      ++state->PendingCount;
      return true;
    }
  };
}

std::unique_ptr<SocketEventLoop> SocketEventLoop::Create()
{
  return std::make_unique<WindowsSocketEventLoop>();
}

#endif
