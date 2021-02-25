/*
  Axel -- A lighter download accelerator for Linux and other Unices

  Copyright 2016      Sjjad Hashemian
  Copyright 2016-2017 Stephen Thirlwall
  Copyright 2017      Antonio Quartulli
  Copyright 2017-2019 Ismael Luceno

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

/* SSL interface */

#include "config.h"
#include <openssl/err.h>
#include "axel.h"

static pthread_mutex_t ssl_lock;
static bool ssl_inited = false;
static conf_t *conf = NULL;

void
ssl_init(conf_t *global_conf)
{
	pthread_mutex_init(&ssl_lock, NULL);
	conf = global_conf;
}

static
void
ssl_startup(void)
{
	pthread_mutex_lock(&ssl_lock);
	if (!ssl_inited) {
		SSL_library_init();
		SSL_load_error_strings();

		ssl_inited = true;
	}
	pthread_mutex_unlock(&ssl_lock);
}

SSL *
ssl_connect(SOCKET fd, char *hostname)
{
	X509 *server_cert;
	SSL_CTX *ssl_ctx;
	SSL *ssl;

	ssl_startup();

	ssl_ctx = SSL_CTX_new(SSLv23_client_method());
	if (!conf->insecure) {
		SSL_CTX_set_default_verify_paths(ssl_ctx);
		SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
	}
	SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);

	ssl = SSL_new(ssl_ctx);
	SSL_set_fd(ssl, fd);
	SSL_set_tlsext_host_name(ssl, hostname);

	int err = SSL_connect(ssl);
	if (err <= 0) {
		fprintf(stderr, _("SSL error: %s %d %d\n"),
			ERR_reason_error_string(ERR_get_error()), err, errno);
		SSL_CTX_free(ssl_ctx);
		return NULL;
	}

	if (conf->insecure) {
		return ssl;
	}

	err = SSL_get_verify_result(ssl);
	if (err != X509_V_OK) {
		fprintf(stderr, _("SSL error: Certificate error\n"));
		SSL_CTX_free(ssl_ctx);
		return NULL;
	}

	server_cert =  SSL_get_peer_certificate(ssl);
	if (server_cert == NULL) {
		fprintf(stderr, _("SSL error: Certificate not found\n"));
		SSL_CTX_free(ssl_ctx);
		return NULL;
	}

	if (!ssl_validate_hostname(hostname, server_cert)) {
		fprintf(stderr, _("SSL error: Hostname verification failed\n"));
		X509_free(server_cert);
		SSL_CTX_free(ssl_ctx);
		return NULL;
	}

	X509_free(server_cert);

	return ssl;
}

void
ssl_disconnect(SSL *ssl)
{
	SSL_shutdown(ssl);
	SSL_free(ssl);
}
