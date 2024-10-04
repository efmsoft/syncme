#if SKTEPOLL
void Socket::EventSignalled(WAIT_RESULT r, uint32_t cookie, bool failed)
{
  // write() will force epoll_wait to exit
  // we have to write a value > 0
  uint64_t value = uint64_t(r) + 1;
  auto s = write(EventDescriptor, &value, sizeof(value));
  if (s != sizeof(value))
  {
    LogE("write failed");
  }
}

void Socket::ResetEventObject()
{
  uint64_t value = 0;
  int n = write(EventDescriptor, &value, sizeof(value));
  if (n != sizeof(value))
  {
    LogosE("write failed");
  }
}

WAIT_RESULT Socket::FastWaitForMultipleObjects(int timeout)
{
  if (GetEventState(Pair->GetExitEvent()) == STATE::SIGNALLED)
    return WAIT_RESULT::OBJECT_0;

  if (GetEventState(Pair->GetCloseEvent()) == STATE::SIGNALLED)
    return WAIT_RESULT::OBJECT_1;

  if (GetEventState(BreakRead) == STATE::SIGNALLED)
    return WAIT_RESULT::OBJECT_3;

  if (GetEventState(StartTX) == STATE::SIGNALLED)
    return WAIT_RESULT::OBJECT_4;

  epoll_event events[2]{};
  int n = epoll_wait(Poll, &events[0], 2, timeout);
  int en = errno;

  if (n < 0)
  {
    if (en != EINTR)
    {
      LogE("epoll_wait failed. Error %i", en);
      return WAIT_RESULT::FAILED;
    }

    n = 0;
  }

  WAIT_RESULT result = WAIT_RESULT::TIMEOUT;

  for (int i = 0; i < n; ++i)
  {
    epoll_event& e = events[i];

    if (e.data.fd == EventDescriptor)
    {
      uint64_t value = 0;
      auto n = read(EventDescriptor, &value, sizeof(value));
      if (n != sizeof(value))
      {
        LogosE("read failed");
      }
      else
        result = WAIT_RESULT(value - 1);

      ResetEventObject();
    }    
    else if (e.data.fd == Handle)
    {
      if (e.events & EPOLLIN)
        EventsMask |= EVENT_READ;

      if (e.events & EPOLLOUT)
        EventsMask |= EVENT_WRITE;
      
      if (e.events & EPOLLRDHUP)
        EventsMask |= EVENT_CLOSE;

      result = WAIT_RESULT::OBJECT_2;
    }
  }

  return result;
}
#endif
