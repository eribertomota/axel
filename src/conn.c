/*
  Axel -- A lighter download accelerator for Linux and other Unices

  Copyright 2001-2007 Wilmer van der Gaast
  Copyright 2007-2008 Y Giridhar Appaji Nag
  Copyright 2008      Philipp Hagemeister
  Copyright 2015      Joao Eriberto Mota Filho
  Copyright 2016      Phillip Berndt
  Copyright 2016      Sjjad Hashemian
  Copyright 2016      Stephen Thirlwall
  Copyright 2017      Antonio Quartulli
  Copyright 2017-2018 Ismael Luceno
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

/* Connection stuff */

#include "config.h"
#include "axel.h"

/**
 * Convert an URL to a conn_t structure.
 */
int
conn_set(conn_t *conn, const char *set_url, bool do_http_encode)
{
	char url[MAX_STRING];
	char *i, *j;

	/* protocol:// */
	if ((i = strstr(set_url, "://")) == NULL) {
		conn->proto = PROTO_DEFAULT;
		conn->port = PROTO_DEFAULT_PORT;
		strlcpy(url, set_url, sizeof(url));
	} else {
		int proto_len = i - set_url;
		if (strncmp(set_url, "ftp", proto_len) == 0) {
			conn->proto = PROTO_FTP;
			conn->port = PROTO_FTP_PORT;
		} else if (strncmp(set_url, "http", proto_len) == 0) {
			conn->proto = PROTO_HTTP;
			conn->port = PROTO_HTTP_PORT;
		} else if (strncmp(set_url, "ftps", proto_len) == 0) {
			conn->proto = PROTO_FTPS;
			conn->port = PROTO_FTPS_PORT;
		} else if (strncmp(set_url, "https", proto_len) == 0) {
			conn->proto = PROTO_HTTPS;
			conn->port = PROTO_HTTPS_PORT;
		} else {
			fprintf(stderr, _("Unsupported protocol\n"));
			return 0;
		}
#ifndef HAVE_SSL
               if (PROTO_IS_SECURE(conn->proto)) {
                       fprintf(stderr,
                               _("Secure protocol is not supported\n"));
                       return 0;
               }
#endif
		strlcpy(url, i + 3, sizeof(url));
	}

	/* Split */
	if ((i = strchr(url, '/')) == NULL) {
		strcpy(conn->dir, "/");
	} else {
		*i = 0;
		snprintf(conn->dir, MAX_STRING, "/%s", i + 1);
		if (do_http_encode && (conn->proto == PROTO_HTTP || conn->proto == PROTO_HTTPS))
			http_encode(conn->dir, sizeof(conn->dir));
	}
	j = strchr(conn->dir, '?');
	if (j != NULL)
		*j = 0;
	i = strrchr(conn->dir, '/');
	if (i != NULL)
		*i = 0;

	if (j != NULL)
		*j = '?';
	if (i == NULL) {
		strlcpy(conn->file, conn->dir, sizeof(conn->file));
		strcpy(conn->dir, "/");
	} else {
		strlcpy(conn->file, i + 1, sizeof(conn->file));
		strlcat(conn->dir, "/", sizeof(conn->dir));
	}

	/* Check for username in host field */
	// FIXME: optimize
	if (strrchr(url, '@') != NULL) {
		strlcpy(conn->user, url, sizeof(conn->user));
		i = strrchr(conn->user, '@');
		*i = 0;
		strlcpy(url, i + 1, sizeof(url));
		*conn->pass = 0;
	} else {
		/* If not: Fill in defaults */
		if (PROTO_IS_FTP(conn->proto)) {
			/* Dash the password: Save traffic by trying
			   to avoid multi-line responses */
			strcpy(conn->user, "anonymous");
			strcpy(conn->pass, "mailto:axel@axel.project");
		} else {
			*conn->user = *conn->pass = 0;
		}
	}

	/* Password? */
	if ((i = strchr(conn->user, ':')) != NULL) {
		*i = 0;
		strlcpy(conn->pass, i + 1, sizeof(conn->pass));
	}

	// Look for IPv6 literal hostname
	if (*url == '[') {
		strlcpy(conn->host, url + 1, sizeof(conn->host));
		if ((i = strrchr(conn->host, ']')) != NULL) {
			*i++ = 0;
		} else {
			return 0;
		}
	} else {
		strlcpy(conn->host, url, sizeof(conn->host));
		i = conn->host;
		while (*i && *i != ':') {
			i++;
		}
	}

	/* Port number? */
	if (*i == ':') {
		*i = 0;
		sscanf(i + 1, "%i", &conn->port);
		i = conn->host;
	}

	return conn->port > 0;
}

const char *
scheme_from_proto(int proto)
{
	switch (proto) {
	case PROTO_FTP:
		return "ftp://";
	case PROTO_FTPS:
		return "ftps://";
	default:
	case PROTO_HTTP:
		return "http://";
	case PROTO_HTTPS:
		return "https://";
	}
}

/* Generate a nice URL string. */
char *
conn_url(char *dst, size_t len, conn_t *conn)
{
	const char *prefix = "", *postfix = "";

	const char *scheme = scheme_from_proto(conn->proto);

	size_t scheme_len = strlcpy(dst, scheme, len);
	if (scheme_len > len)
		return NULL;

	len -= scheme_len;

	char *p = dst + scheme_len;

	if (*conn->user != 0 && strcmp(conn->user, "anonymous") != 0) {
		int plen = snprintf(p, len, "%s:%s@", conn->user, conn->pass);
		if (plen < 0)
			return NULL;
		len -= plen;
		p += plen;
	}

	if (is_ipv6_addr(conn->host)) {
		prefix = "[";
		postfix = "]";
	}

	int plen;
	plen = snprintf(p, len, "%s%s%s:%i%s%s", prefix, conn->host, postfix,
			conn->port, conn->dir, conn->file);

	return plen < 0 ? NULL : dst;
}

/* Simple... */
void
conn_disconnect(conn_t *conn)
{
	if (PROTO_IS_FTP(conn->proto) && !conn->proxy)
		ftp_disconnect(conn->ftp);
	else
		http_disconnect(conn->http);
	conn->tcp = NULL;
	conn->enabled = false;
}

int
conn_init(conn_t *conn)
{
	char *proxy = conn->conf->http_proxy, *host = conn->conf->no_proxy;
	int i;

	if (*conn->conf->http_proxy == 0) {
		proxy = NULL;
	} else if (*conn->conf->no_proxy != 0) {
		for (i = 0;; i++)
			if (conn->conf->no_proxy[i] == 0) {
				if (strstr(conn->host, host) != NULL)
					proxy = NULL;
				host = &conn->conf->no_proxy[i + 1];
				if (conn->conf->no_proxy[i + 1] == 0)
					break;
			}
	}

	conn->proxy = proxy != NULL;

	if (PROTO_IS_FTP(conn->proto) && !conn->proxy) {
		conn->ftp->local_if = conn->local_if;
		conn->ftp->ftp_mode = FTP_PASSIVE;
		conn->ftp->tcp.ai_family = conn->conf->ai_family;
		if (!ftp_connect(conn->ftp, conn->proto, conn->host, conn->port,
				 conn->user, conn->pass,
				 conn->conf->io_timeout)) {
			conn->message = conn->ftp->message;
			conn_disconnect(conn);
			return 0;
		}
		conn->message = conn->ftp->message;
		if (!ftp_cwd(conn->ftp, conn->dir)) {
			conn_disconnect(conn);
			return 0;
		}
	} else {
		conn->http->local_if = conn->local_if;
		conn->http->tcp.ai_family = conn->conf->ai_family;
		if (!http_connect(conn->http, conn->proto, proxy, conn->host,
				  conn->port, conn->user, conn->pass,
				  conn->conf->io_timeout)) {
			conn->message = conn->http->headers->p;
			conn_disconnect(conn);
			return 0;
		}
		conn->message = conn->http->headers->p;
		conn->tcp = &conn->http->tcp;
	}
	return 1;
}

/**
 * Setup the connection.
 *
 * Must be called with the conn_t lock held.
 */
int
conn_setup(conn_t *conn)
{
	if (conn->ftp->tcp.fd <= 0 && conn->http->tcp.fd <= 0)
		if (!conn_init(conn))
			return 0;

	if (PROTO_IS_FTP(conn->proto) && !conn->proxy) {
		/* Set up data connnection */
		if (!ftp_data(conn->ftp, conn->conf->io_timeout))
			return 0;
		conn->tcp = &conn->ftp->data_tcp;

		if (conn->currentbyte) {
			ftp_command(conn->ftp, "REST %jd", conn->currentbyte);
			if (ftp_wait(conn->ftp) / 100 != 3 &&
			    conn->ftp->status / 100 != 2)
				return 0;
		}
	} else {
		char s[MAX_STRING * 2];
		int i;

		snprintf(s, sizeof(s), "%s%s", conn->dir, conn->file);
		conn->http->firstbyte =
			conn->supported ? conn->currentbyte : -1;
		conn->http->lastbyte = conn->lastbyte;

		abuf_setup(conn->http->request, 2048);
		http_get(conn->http, s);
		for (i = 0; i < conn->conf->add_header_count; i++)
			http_addheader(conn->http, "%s",
				       conn->conf->add_header[i]);
	}
	return 1;
}

int
conn_exec(conn_t *conn)
{
	if (PROTO_IS_FTP(conn->proto) && !conn->proxy) {
		if (!ftp_command(conn->ftp, "RETR %s", conn->file))
			return 0;
		return ftp_wait(conn->ftp) / 100 == 1;
	} else {
		abuf_setup(conn->http->headers, 1024);
		if (!http_exec(conn->http))
			return 0;
		return conn->http->status / 100 == 2;
	}
}

static
int
conn_info_ftp(conn_t *conn)
{
	ftp_command(conn->ftp, "REST %d", 1);
	if (ftp_wait(conn->ftp) / 100 == 3 ||
	    conn->ftp->status / 100 == 2) {
		conn->supported = true;
		ftp_command(conn->ftp, "REST %d", 0);
		ftp_wait(conn->ftp);
	} else {
		conn->supported = false;
	}

	if (!ftp_cwd(conn->ftp, conn->dir))
		return 0;
	conn->size = ftp_size(conn->ftp, conn->file,
			      conn->conf->max_redirect,
			      conn->conf->io_timeout);
	if (conn->size < 0)
		conn->supported = false;
	if (conn->size == -1)
		return 0;
	else if (conn->size == -2)
		conn->size = LLONG_MAX;

	return 1;
}

/* Get file size and other information */
int
conn_info(conn_t *conn)
{
	/* It's all a bit messed up.. But it works. */
	if (PROTO_IS_FTP(conn->proto) && !conn->proxy) {
		return conn_info_ftp(conn);
	}

	char s[1005];
	long long int i = 0;

	do {
		const char *t;

		conn->supported = true;
		conn->currentbyte = 0;
		pthread_mutex_lock(&conn->lock);
		if (!conn_setup(conn))
			return 0;
		pthread_mutex_unlock(&conn->lock);
		conn_exec(conn);
		conn_disconnect(conn);

		http_filename(conn->http, conn->output_filename);

		/* Code 3xx == redirect */
		if (conn->http->status / 100 != 3)
			break;
		if ((t = http_header(conn->http, "location:")) == NULL)
			return 0;
		sscanf(t, "%1000s", s);
		if (s[0] == '/') {
			abuf_printf(conn->http->headers, "%s%s:%i%s",
				    scheme_from_proto(conn->proto),
				    conn->host, conn->port, s);
			strlcpy(s, conn->http->headers->p, sizeof(s));
		} else if (strstr(s, "://") == NULL) {
			conn_url(conn->http->headers->p,
				 conn->http->headers->len,
				 conn);
			strlcat(conn->http->headers->p, s,
				conn->http->headers->len);
			strlcpy(s, conn->http->headers->p, sizeof(s));
		}

		if (!conn_set(conn, s, true)) {
			return 0;
		}

		/* check if the download has been redirected to FTP and
		 * report it back to the caller */
		if (PROTO_IS_FTP(conn->proto) && !conn->proxy) {
			return -1;
		}

		if (++i >= conn->conf->max_redirect) {
			fprintf(stderr, _("Too many redirects.\n"));
			return 0;
		}
	} while (conn->http->status / 100 == 3);

	/* Check for non-recoverable errors */
	if (conn->http->status != 416 && conn->http->status / 100 != 2)
		return 0;

	conn->size = http_size_from_range(conn->http);
	/* We assume partial requests are supported if a Content-Range
	 * header is present.
	 */
	conn->supported = conn->http->status == 206 || conn->size > 0;

	if (conn->size <= 0) {
		/* Sanity check */
		switch (conn->http->status) {
		case 200: /* OK -> unsupported */
		case 416: /* Range Not Satisfiable -> broken? */
			conn->supported = false;
		case 206: /* Partial Content */
			break;
		default: /* unexpected */
			return 0;
		}
	}
	/* If Content-Range is missing or disagrees with Content-Length, it's a
	 * server bug; we take the larger and hope for the best.
	 */
	conn->size = max(conn->size, http_size(conn->http));
	if (conn->size <= 0) {
		conn->size = LLONG_MAX;
		conn->supported = false;
	}
	return 1;
}

int
conn_info_status_get(char *msg, size_t size, conn_t *conn)
{
	if (is_proto_http(conn->proto)) {
		char *p = conn->http->headers->p;
		/* Skip protocol and code */
		while (*p++ != ' ');
		while (*p++ != ' ');
		size_t len = strcspn(p, "\r\n");
		if (len) {
			strlcpy(msg, p, min(len + 1, size));
			return conn->http->status;
		}
	}
	strlcpy(msg, _("Unknown Error"), size);
	return -1;
}
