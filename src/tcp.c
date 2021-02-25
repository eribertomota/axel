/*
  Axel -- A lighter download accelerator for Linux and other Unices

  Copyright 2001-2007 Wilmer van der Gaast
  Copyright 2010      Mark Smith
  Copyright 2016-2017 Stephen Thirlwall
  Copyright 2017      Antonio Quartulli
  Copyright 2017-2019 Ismael Luceno
  Copyright 2018      Shankar
  Copyright 2019      Park Ju Hyung

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  In addition, as a special exception, the copyright holders give
  permission to link the code of portions of this program with the
  OpenSSL library under certain conditions as described in each
  individual source file, and distribute linked combinations including
  the two.

  You must obey the GNU General Public License in all respects for all
  of the code used other than OpenSSL. If you modify file(s) with this
  exception, you may extend this exception to your version of the
  file(s), but you are not obligated to do so. If you do not wish to do
  so, delete this exception statement from your version. If you delete
  this exception statement from all source files in the program, then
  also delete it here.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/* TCP control file */

#define _POSIX_C_SOURCE 200112L

#include "config.h"
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <netinet/tcp.h>
#endif
#include "axel.h"

#ifndef TCP_FASTOPEN_CONNECT
#ifdef __linux__
#define TCP_FASTOPEN_CONNECT 30
#else /* __linux__ */
#define TCP_FASTOPEN_CONNECT 0
#endif /* __linux__ */
#endif /* !TCP_FASTOPEN_CONNECT */

/*
 * Check if the given hostname is ipv6 literal
 * Returns 1 if true and 0 if false
 */
int is_ipv6_addr(const char *hostname) {
	char buf[16]; /* Max buff size needed for inet_pton() */

	return hostname && 1 == inet_pton(AF_INET6, hostname, buf);
}

inline static void
tcp_error(char *hostname, int port, const char *reason)
{
	fprintf(stderr, _("Unable to connect to server %s:%i: %s\n"),
		hostname, port, reason);
}

/* Get a TCP connection */
int
tcp_connect(tcp_t *tcp, char *hostname, int port, int secure, char *local_if,
	    unsigned io_timeout)
{
	struct sockaddr_in local_addr;
	char portstr[10];
	struct addrinfo ai_hints;
	struct addrinfo *gai_results, *gai_result;
	int ret;
	SOCKET sock_fd = INVALID_SOCKET;

	memset(&local_addr, 0, sizeof(local_addr));
	if (local_if) {
		if (!*local_if || tcp->ai_family != AF_INET) {
			local_if = NULL;
		} else {
			local_addr.sin_family = AF_INET;
			local_addr.sin_port = 0;
			local_addr.sin_addr.s_addr = inet_addr(local_if);
		}
	}

	snprintf(portstr, sizeof(portstr), "%d", port);

	memset(&ai_hints, 0, sizeof(ai_hints));
	ai_hints.ai_family = tcp->ai_family;
	ai_hints.ai_socktype = SOCK_STREAM;
	ai_hints.ai_flags = AI_ADDRCONFIG;
	ai_hints.ai_protocol = 0;

	ret = getaddrinfo(hostname, portstr, &ai_hints, &gai_results);
	if (ret != 0) {
		tcp_error(hostname, port, gai_strerror(ret));
		return -1;
	}

	gai_result = gai_results;
	do {
		int tcp_fastopen = -1;

		if (sock_fd != INVALID_SOCKET) {
			closesocket(sock_fd);
			sock_fd = INVALID_SOCKET;
		}
		sock_fd = socket(gai_result->ai_family,
				 gai_result->ai_socktype,
				 gai_result->ai_protocol);
		if (sock_fd == INVALID_SOCKET)
			continue;

		if (local_if && gai_result->ai_family == AF_INET) {
			bind(sock_fd, (struct sockaddr *)&local_addr,
			     sizeof(local_addr));
			/* FIXME report errors */
		}

		if (TCP_FASTOPEN_CONNECT) {
			tcp_fastopen = setsockopt(sock_fd, IPPROTO_TCP,
						  TCP_FASTOPEN_CONNECT,
						  NULL, 0);
		} else if (io_timeout) {
			/* Set O_NONBLOCK so we can timeout */
			fcntl(sock_fd, F_SETFL, O_NONBLOCK);
		}
		ret = connect(sock_fd, gai_result->ai_addr,
			      gai_result->ai_addrlen);

		/* Already connected maybe? */
		if (ret != -1)
			break;

#ifdef _WIN32
		if (WSAEWOULDBLOCK != WSAGetLastError())
#else
		if (errno != EINPROGRESS)
#endif
			continue;

		/* With TFO we must assume success */
		if (tcp_fastopen != -1)
			break;

		/* Wait for the connection */
		fd_set fdset;
		FD_ZERO(&fdset);
		FD_SET(sock_fd, &fdset);
		struct timeval tout = { .tv_sec  = io_timeout };
		ret = select(sock_fd + 1, NULL, &fdset, NULL, &tout);
		/* Success? */
		if (ret != -1)
			break;
	} while ((gai_result = gai_result->ai_next));

	freeaddrinfo(gai_results);

	if (sock_fd == INVALID_SOCKET) {
		tcp_error(hostname, port, strerror(errno));
		return -1;
	}

	fcntl(sock_fd, F_SETFL, 0);

#ifdef HAVE_SSL
	if (secure) {
		tcp->ssl = ssl_connect(sock_fd, hostname);
		if (tcp->ssl == NULL) {
			closesocket(sock_fd);
			return -1;
		}
	}
#endif				/* HAVE_SSL */
	tcp->fd = sock_fd;

	/* Set I/O timeout */
	SET_SOCK_TIMEOUT(sock_fd, SO_RCVTIMEO, io_timeout);
	SET_SOCK_TIMEOUT(sock_fd, SO_SNDTIMEO, io_timeout);

	return 1;
}

ssize_t
tcp_read(tcp_t *tcp, void *buffer, int size)
{
#ifdef HAVE_SSL
	if (tcp->ssl != NULL)
		return SSL_read(tcp->ssl, buffer, size);
	else
#endif				/* HAVE_SSL */
		return recv(tcp->fd, buffer, size, 0);
}

ssize_t
tcp_write(tcp_t *tcp, void *buffer, int size)
{
	int ret;
#ifdef HAVE_SSL
	if (tcp->ssl != NULL)
		ret = SSL_write(tcp->ssl, buffer, size);
	else
#endif				/* HAVE_SSL */
		ret = send(tcp->fd, buffer, size, 0);

#ifdef _WIN32
	if (ret < 0 && WSAENOTCONN == WSAGetLastError()) {
		errno = EAGAIN;
	}
#endif
	return ret;
}

void
tcp_close(tcp_t *tcp)
{
	if (tcp->fd != INVALID_SOCKET) {
#ifdef HAVE_SSL
		if (tcp->ssl != NULL) {
			ssl_disconnect(tcp->ssl);
			tcp->ssl = NULL;
		}
#endif
		closesocket(tcp->fd);
		tcp->fd = INVALID_SOCKET;
	}
}

#ifdef _WIN32
int
get_if_ip(char *dst, size_t len, const char *iface)
{
	char name[155];
    PHOSTENT hostinfo;
	int ret = 0;

	if (!gethostname(name, sizeof(name)))
		return 0;

	/* FIXME gethostbyname should be replaced */
    if ((hostinfo = gethostbyname(name))) {
		strlcpy(dst, inet_ntoa(*(struct in_addr *)*hostinfo->h_addr_list), len);
		ret = 1;
    }

	return ret;
}
#else /* !_WIN32 */
int
get_if_ip(char *dst, size_t len, const char *iface)
{
	struct ifreq ifr;
	int ret, fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);

	if (fd < 0)
		return 0;

	memset(&ifr, 0, sizeof(struct ifreq));

	strlcpy(ifr.ifr_name, iface, sizeof(ifr.ifr_name));
	ifr.ifr_addr.sa_family = AF_INET;

	ret = !ioctl(fd, SIOCGIFADDR, &ifr);
	if (ret) {
		struct sockaddr_in *x = (struct sockaddr_in *)&ifr.ifr_addr;
		strlcpy(dst, inet_ntoa(x->sin_addr), len);
	}
	close(fd);

	return ret;
}
#endif /* !_WIN32 */
