#include <cassert>
#include <stdio.h>
#include <thread>

#include <Syncme/Logger/Log.h>
#include <Syncme/Sleep.h>
#include <Syncme/Sockets/API.h>
#include <Syncme/Sync.h>

using namespace Syncme;

const int ServerPort = 12345;

void Server(HEvent readyEvent)
{
  int s = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (s == -1)
  {
    LogosE("socket() failed");
    exit(1);
  }

  int reuse = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
  {
    LogosE("setsockopt(SO_REUSEADDR) failed");
    exit(1);
  }

  linger lin{};
  lin.l_onoff = true;
  lin.l_linger = 0;
  if (setsockopt(s, SOL_SOCKET, SO_LINGER, (const char*)&lin, sizeof(lin)) < 0)
  {
    LogosE("setsockopt(SO_LINGER) failed");
    exit(1);
  }
  
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(ServerPort);
  int rc = bind(s, (struct sockaddr*)&addr, sizeof(addr));
  if (rc == -1)
  {
    LogosE("bind() failed");
    exit(1);
  }

  rc = listen(s, 1);
  if (rc == -1)
  {
    LogosE("listen() failed");
    exit(1);
  }

  printf("server: client connected\n");
  SetEvent(readyEvent);

  sockaddr_in peer{};
  socklen_t len = sizeof(peer);
  int client = (int)accept(s, (sockaddr*)&peer, &len);
  if (client == -1)
  {
    LogosE("accept() failed");
    exit(1);
  }

  unsigned long on = 1;
  rc = ioctlsocket(client, (int)FIONBIO, &on);
  if (rc == -1)
  {
    LogosE("ioctlsocket(FIONBIO) failed");
    exit(1);
  }

  HEvent e = CreateSocketEvent(client, EVENT_READ | EVENT_CLOSE);

  // Now client socket in non-blocking mode. We have to wait for EVENT_READ
  // before recv() call

  auto wr = WaitForSingleObject(e);
  assert(wr == WAIT_RESULT::OBJECT_0);

  int mask = GetSocketEvents(e);
  assert(mask & EVENT_READ);

  printf("server: socket event was signalled. mask = %i\n", mask);

  char buf[256]{};
  int n = recv(client, buf, sizeof(buf), 0);
  if (n <= 0)
  {
    LogosE("recv() failed");
    exit(1);
  }

  printf("server: received %i bytes %s\n", n, buf);

  std::string str(buf);
  if (str != "hello")
  {
    printf("unsupported command!\n");
  }

  wr = WaitForSingleObject(e, 3000);
  assert(wr == WAIT_RESULT::TIMEOUT);

  // Send it back
  rc = send(client, "hello", 5, 0);
  if (rc == -1)
  {
    LogosE("send() failed");
    exit(1);
  }

  wr = WaitForSingleObject(e);
  assert(wr == WAIT_RESULT::OBJECT_0);

  *buf = 0;
  n = recv(client, buf, sizeof(buf), 0);
  if (n <= 0)
  {
    LogosE("recv() failed");
    exit(1);
  }

  printf("server: received %i bytes %s\n", n, buf);

  CloseHandle(e);
  closesocket(client);
  closesocket(s);
}

void Client()
{
  int h = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (h == -1)
  {
    LogosE("socket() failed");
    exit(1);
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(ServerPort);
  int rc = connect(h, (sockaddr*)&addr, sizeof(addr));
  if (rc == -1)
  {
    LogosE("connect() failed");
    exit(1);
  }

  rc = send(h, "hello", 5, 0);
  if (rc == -1)
  {
    LogosE("send() failed");
    exit(1);
  }

  // The socket in blocking mode. Just wait for server's response

  char buf[256]{};
  rc = recv(h, buf, sizeof(buf), 0);
  if (rc == -1)
  {
    LogosE("recv() failed");
    exit(1);
  }

  printf("client: received %s\n", buf);

  Sleep(3000);

  rc = send(h, "hello2", 6, 0);
  if (rc == -1)
  {
    LogosE("send() failed");
    exit(1);
  }

  closesocket(h);
}

int main()
{
#ifdef _WIN32
  WSADATA wsaData{};
  WORD wVersionRequested = MAKEWORD(2, 2);
  int e = WSAStartup(wVersionRequested, &wsaData);
  if (e)
  {
    LogosE("WSAStartup() failed");
    exit(1);
  }
#endif

  HEvent readyEvent = CreateNotificationEvent();
  std::jthread srever(&Server, readyEvent);

  WaitForSingleObject(readyEvent);

  Client();
  printf("done\n");

#ifdef _WIN32
  WSACleanup();
#endif

  return 0;
}