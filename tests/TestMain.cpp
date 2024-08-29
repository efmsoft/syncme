#include <gtest/gtest.h>

#include <Syncme/Sockets/API.h>

#include "Initialization.h"

InitializationItem* InitializationListHead;

#ifndef _WIN32
void BlockSignal(int signal_to_block)
{
  sigset_t old_state;
  sigprocmask(SIG_BLOCK, nullptr, &old_state);

  sigset_t set = old_state;
  sigaddset(&set, signal_to_block);

  pthread_sigmask(SIG_BLOCK, &set, nullptr);
}
#endif

int main(int argc, char* argv[])
{
  ::testing::InitGoogleTest(&argc, argv);

#ifdef _WIN32
  WSADATA wsaData{};
  WORD wVersionRequested = MAKEWORD(2, 2);
  int e = WSAStartup(wVersionRequested, &wsaData);
  EXPECT_EQ(e, 0);
#else
  BlockSignal(SIGPIPE);
#endif

  for (auto* p = InitializationListHead; p; p = p->Next)
  {
    if (p->InitRoutine() == false)
      exit(1);
  }

  int rc = RUN_ALL_TESTS();

  for (auto* p = InitializationListHead; p; p = p->Next)
    p->ExitRoutine();

#ifdef _WIN32
  WSACleanup();
#endif

  return rc;
}