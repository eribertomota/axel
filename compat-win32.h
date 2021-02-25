#pragma once

#ifdef _WIN32

/* headers */
#include <stdint.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#define WSAINIT() { \
    WSADATA wsaData; \
    if (WSAStartup(MAKEWORD(2, 1), &wsaData) != NO_ERROR) { \
        wprintf(L"Error on WSAStartup()\n"); \
        return -1; \
    } \
}

#define WSAClose() WSACleanup()

/* Make sa_family_t compatable */
typedef u_short sa_family_t;

/* 0 for BLOCK and !0 for NONBLOCK */
#define O_NONBLOCK (u_long)1

#define F_SETFL FIONBIO
int fcntl(fd, flag, mode) {
	u_int m = mode;
	return ioctlsocket(fd, flag, &m);
}

/* set socket timeout */
static inline int
SET_SOCK_TIMEOUT(SOCKET s, int optname, unsigned int io_timeout)
{
	char optval[32];
	sprintf(optval, "%u000", io_timeout);	/* second to millisecond */
	return setsockopt(s, SOL_SOCKET, optname, optval, sizeof(optval));
}

#else /* _WIN32 */

#define WSAINIT()
#define WSAClose()

typedef int SOCKET;
#define INVALID_SOCKET -1

#define closesocket(sock_fd) close(sock_fd)

static inline int
SET_SOCK_TIMEOUT(SOCKET s, int optname, unsigned int io_timeout)
{
	struct timeval tval = {.tv_sec = io_timeout };
	return setsockopt(s, SOL_SOCKET, optname, &tval, sizeof(tval));
}

#endif /* _WIN32 */

#define SOCK_ISINVALID(sock) (sock == 0 || sock == INVALID_SOCKET)
