#ifndef _WIN32

#include <cassert>
#include <atomic>
#include <deque>
#include <errno.h>
#include <mutex>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sockets/SocketEventLoop.h>

using namespace Syncme;

namespace
{
  struct Entry
  {
    Socket* Skt;
    void* Context;
    int Events;

    Entry()
      : Skt(nullptr)
      , Context(nullptr)
      , Events(0)
    {
    }
  };

  class LinuxSocketEventLoop : public SocketEventLoop
  {
    int Poll;
    int StopEvent;
    std::atomic<bool> Stopping;
    std::mutex Lock;
    std::unordered_map<int, Entry> Entries;
    std::vector<epoll_event> Events;
    std::deque<SocketEventLoopResult> PendingResults;

  public:
    LinuxSocketEventLoop()
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

    ~LinuxSocketEventLoop() override
    {
      if (StopEvent != -1 && Poll != -1)
      {
        epoll_ctl(Poll, EPOLL_CTL_DEL, StopEvent, nullptr);
      }

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

    bool Add(Socket* socket, void* context, int events) override
    {
      if (Poll == -1 || socket == nullptr)
        return false;

      int fd = socket->GetFD();
      if (fd == -1)
        return false;

      Entry entry;
      entry.Skt = socket;
      entry.Context = context;
      entry.Events = events;

      epoll_event ev = MakeEvent(fd, events);

      std::lock_guard<std::mutex> guard(Lock);
      if (epoll_ctl(Poll, EPOLL_CTL_ADD, fd, &ev) == -1)
      {
        LogosE("epoll_ctl(EPOLL_CTL_ADD) failed");
        return false;
      }

      Entries[fd] = entry;
      return true;
    }

    bool Update(Socket* socket, int events) override
    {
      if (Poll == -1 || socket == nullptr)
        return false;

      int fd = socket->GetFD();
      std::lock_guard<std::mutex> guard(Lock);
      auto it = Entries.find(fd);
      if (it == Entries.end())
        return false;

      it->second.Events = events;

      epoll_event ev = MakeEvent(fd, events);
      if (epoll_ctl(Poll, EPOLL_CTL_MOD, fd, &ev) == -1)
      {
        LogosE("epoll_ctl(EPOLL_CTL_MOD) failed");
        return false;
      }

      return true;
    }

    bool Remove(Socket* socket) override
    {
      if (Poll == -1 || socket == nullptr)
        return false;

      int fd = socket->GetFD();
      std::lock_guard<std::mutex> guard(Lock);
      auto it = Entries.find(fd);
      if (it == Entries.end())
        return false;

      Entries.erase(it);

      if (epoll_ctl(Poll, EPOLL_CTL_DEL, fd, nullptr) == -1)
      {
        LogosE("epoll_ctl(EPOLL_CTL_DEL) failed");
        return false;
      }

      return true;
    }

    bool Wait(SocketEventLoopResult& result, int timeout) override
    {
      result = SocketEventLoopResult();

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
        {
          epoll_event& ev = Events[i];
          if (ev.data.fd == StopEvent)
          {
            uint64_t value = 0;
            read(StopEvent, &value, sizeof(value));
            if (Stopping.load())
            {
              result.Operation = SocketEventLoopOperation::Stop;
              result.Events = EVENT_CLOSE;
              return false;
            }

            QueueWakeResult();
            continue;
          }

          QueueSocketResult(ev);
        }

        if (PopPendingResult(result))
          return true;
      }
    }

    void Wake() override
    {
      if (StopEvent == -1)
        return;

      uint64_t value = 1;
      write(StopEvent, &value, sizeof(value));
    }

    void Stop() override
    {
      Stopping.store(true);
      Wake();
    }

  private:
    bool PopPendingResult(SocketEventLoopResult& result)
    {
      std::lock_guard<std::mutex> guard(Lock);
      if (PendingResults.empty())
        return false;

      result = PendingResults.front();
      PendingResults.pop_front();
      return true;
    }

    void QueueWakeResult()
    {
      SocketEventLoopResult result;
      result.Operation = SocketEventLoopOperation::Wake;

      std::lock_guard<std::mutex> guard(Lock);
      PendingResults.push_back(result);
    }

    void QueueSocketResult(const epoll_event& ev)
    {
      SocketEventLoopResult result;

      {
        std::lock_guard<std::mutex> guard(Lock);
        auto it = Entries.find(ev.data.fd);
        if (it == Entries.end())
          return;

        int events = 0;
        if (ev.events & EPOLLIN)
          events |= EVENT_READ;

        if (ev.events & EPOLLOUT)
          events |= EVENT_WRITE;

        if (ev.events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
          events |= EVENT_CLOSE;

        if (events == 0)
          return;

        result.Skt = it->second.Skt;
        result.Context = it->second.Context;
        result.Events = events;

        if (events & EVENT_CLOSE)
          result.Operation = SocketEventLoopOperation::Close;
        else if (events & EVENT_READ)
          result.Operation = SocketEventLoopOperation::Read;
        else if (events & EVENT_WRITE)
          result.Operation = SocketEventLoopOperation::Write;

        PendingResults.push_back(result);
      }
    }

    static epoll_event MakeEvent(int fd, int events)
    {
      epoll_event ev{};
      ev.data.fd = fd;

      if (events & EVENT_READ)
        ev.events |= EPOLLIN;

      if (events & EVENT_WRITE)
        ev.events |= EPOLLOUT;

      if (events & EVENT_CLOSE)
        ev.events |= EPOLLRDHUP;

      return ev;
    }
  };
}

std::unique_ptr<SocketEventLoop> SocketEventLoop::Create()
{
  return std::make_unique<LinuxSocketEventLoop>();
}

#endif
