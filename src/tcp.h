/*
  Axel -- A lighter download accelerator for Linux and other Unices

  Copyright 2001-2007 Wilmer van der Gaast
  Copyright 2016      Stephen Thirlwall
  Copyright 2017      Antonio Quartulli
  Copyright 2017-2019 Ismael Luceno
  Copyright 2018      Shankar

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

/* TCP control include file */

#ifndef AXEL_TCP_H
#define AXEL_TCP_H

#ifdef HAVE_SSL
#include <openssl/ssl.h>
#endif

typedef struct {
	SOCKET fd;
	sa_family_t ai_family;
#ifdef HAVE_SSL
	SSL *ssl;
#endif
} tcp_t;

int is_ipv6_addr(const char *hostname);
int tcp_connect(tcp_t *tcp, char *hostname, int port, int secure,
		char *local_if, unsigned io_timeout);
void tcp_close(tcp_t *tcp);

ssize_t tcp_read(tcp_t *tcp, void *buffer, int size);
ssize_t tcp_write(tcp_t *tcp, void *buffer, int size);

int get_if_ip(char *dst, size_t len, const char *iface);

#endif				/* AXEL_TCP_H */
