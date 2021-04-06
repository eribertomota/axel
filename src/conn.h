/*
  Axel -- A lighter download accelerator for Linux and other Unices

  Copyright 2001-2007 Wilmer van der Gaast
  Copyright 2008      Y Giridhar Appaji Nag
  Copyright 2016      Sjjad Hashemian
  Copyright 2016      Stephen Thirlwall
  Copyright 2017      Antonio Quartulli
  Copyright 2017      Ismael Luceno

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

/* Connection stuff */

#ifndef AXEL_CONN_H
#define AXEL_CONN_H

#define PROTO_SECURE_MASK	(1<<0)	/* bit 0 - 0 = insecure, 1 = secure */
#define PROTO_PROTO_MASK	(1<<1)	/* bit 1 = 0 = ftp,      1 = http   */

#define PROTO_INSECURE		(0<<0)
#define PROTO_SECURE		(1<<0)
#define PROTO_PROTO_FTP		(0<<1)
#define PROTO_PROTO_HTTP	(1<<1)

#define PROTO_IS_FTP(proto) \
	(((proto) & PROTO_PROTO_MASK) == PROTO_PROTO_FTP)
#define PROTO_IS_SECURE(proto) \
	(((proto) & PROTO_SECURE_MASK) == PROTO_SECURE)

#define PROTO_FTP		(PROTO_PROTO_FTP|PROTO_INSECURE)
#define	PROTO_FTP_PORT		21

#define PROTO_FTPS		(PROTO_PROTO_FTP|PROTO_SECURE)
#define	PROTO_FTPS_PORT		990

#define PROTO_HTTP		(PROTO_PROTO_HTTP|PROTO_INSECURE)
#define	PROTO_HTTP_PORT		80

#define PROTO_HTTPS		(PROTO_PROTO_HTTP|PROTO_SECURE)
#define	PROTO_HTTPS_PORT	443

#define PROTO_DEFAULT          PROTO_HTTP
#define PROTO_DEFAULT_PORT     PROTO_HTTP_PORT

static inline
int
is_proto_http(int proto)
{
	return (proto & PROTO_PROTO_MASK) == PROTO_PROTO_HTTP;
}

typedef struct {
	conf_t *conf;

	int proto;
	int port;
	int proxy;
	char host[MAX_STRING];
	char dir[MAX_STRING];
	char file[MAX_STRING];
	char user[MAX_STRING];
	char pass[MAX_STRING];
	char output_filename[MAX_STRING];

	ftp_t ftp[1];
	http_t http[1];
	off_t size; /* File size, not 'connection size'.. */
	off_t currentbyte;
	off_t lastbyte;
	tcp_t *tcp;
	bool enabled;
	bool supported;
	int last_transfer;
	char *message;
	char *local_if;

	bool state;
	pthread_t setup_thread[1];
	pthread_mutex_t lock;
} conn_t;

int conn_set(conn_t *conn, const char *set_url, bool do_http_encode);
char *conn_url(char *dst, size_t len, conn_t *conn);
void conn_disconnect(conn_t *conn);
int conn_init(conn_t *conn);
int conn_setup(conn_t *conn);
int conn_exec(conn_t *conn);
int conn_info(conn_t *conn);
int conn_info_status_get(char *msg, size_t size, conn_t *conn);
const char *scheme_from_proto(int proto);

#endif				/* AXEL_CONN_H */
