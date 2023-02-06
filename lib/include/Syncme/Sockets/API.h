#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>

#define SOCKADDR_IN_ADDR(pa) (pa)->sin_addr.S_un.S_addr
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#define SD_SEND SHUT_WR
#define SD_RECEIVE SHUT_RD

#define SOCKADDR_IN_ADDR(pa) (pa)->sin_addr.s_addr
#endif

#ifndef _WIN32
#define ioctlsocket ioctl
#define closesocket close
#endif

#include <Syncme/Sockets/SSLHelpers.h>
