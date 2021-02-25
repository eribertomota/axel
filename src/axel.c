/*
  Axel -- A lighter download accelerator for Linux and other Unices

  Copyright 2001-2007 Wilmer van der Gaast
  Copyright 2007-2009 Y Giridhar Appaji Nag
  Copyright 2008-2009 Philipp Hagemeister
  Copyright 2015-2017 Joao Eriberto Mota Filho
  Copyright 2016      Denis Denisov
  Copyright 2016      Ivan Gimenez
  Copyright 2016      Sjjad Hashemian
  Copyright 2016      Stephen Thirlwall
  Copyright 2017      Antonio Quartulli
  Copyright 2017-2019 Ismael Luceno
  Copyright 2017      nemermollon
  Copyright 2018      Shankar
  Copyright 2019      Evangelos Foutras

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

/* Main control */

#include "config.h"
#include "axel.h"
#include "assert.h"
#include "sleep.h"

/* Axel */
static void save_state(axel_t *axel);
static void *setup_thread(void *);

#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif /* __GNUC__ */
static void axel_message(axel_t *axel, const char *format, ...);
static void axel_divide(axel_t *axel);

static char *buffer = NULL;

#define MIN_CHUNK_WORTH (100 * 1024) /* 100 KB */


static
char *
stfile_makename(const char *bname)
{
	const char suffix[] = ".st";
	const size_t bname_len = strlen(bname);
	char *buf = malloc(bname_len + sizeof(suffix));
	if (!buf) {
		perror("stfile_open");
		abort();
	}
	memcpy(buf, bname, bname_len);
	memcpy(buf + bname_len, suffix, sizeof(suffix));
	return buf;
}


static
int
stfile_unlink(const char *bname)
{
	char *stname = stfile_makename(bname);
	int ret = unlink(stname);
	free(stname);
	return ret;
}

static
int
stfile_access(const char *bname, int mode)
{
	char *stname = stfile_makename(bname);
	int ret = access(stname, mode);
	free(stname);
	return ret;
}


static
int
stfile_open(const char *bname, int flags, mode_t mode)
{
	char *stname = stfile_makename(bname);
	int fd = open(stname, flags, mode);
	free(stname);
	return fd;
}


/* Create a new axel_t structure */
axel_t *
axel_new(conf_t *conf, int count, const search_t *res)
{
	axel_t *axel;
	int status;
	uint64_t delay;
	url_t *u;
	char *s;
	int i;

	if (!count || !res)
		return NULL;

	axel = calloc(1, sizeof(axel_t));
	if (!axel)
		goto nomem;

	axel->conf = conf;
	axel->conn = calloc(axel->conf->num_connections, sizeof(conn_t));
	if (!axel->conn)
		goto nomem;

	for (i = 0; i < axel->conf->num_connections; i++)
		pthread_mutex_init(&axel->conn[i].lock, NULL);

	if (axel->conf->max_speed > 0) {
		/* max_speed / buffer_size < .5 */
		if (16 * axel->conf->max_speed / axel->conf->buffer_size < 8) {
			if (axel->conf->verbose >= 2)
				axel_message(axel,
					     _("Buffer resized for this speed."));
			axel->conf->buffer_size = axel->conf->max_speed;
		}
		delay = 1000000000 * axel->conf->buffer_size *
			axel->conf->num_connections / axel->conf->max_speed;

		axel->delay_time.tv_sec  = delay / 1000000000;
		axel->delay_time.tv_nsec = delay % 1000000000;
	}
	if (buffer == NULL) {
		buffer = malloc(axel->conf->buffer_size);
		if (!buffer)
			goto nomem;
	}

	u = malloc(sizeof(url_t) * count);
	if (!u)
		goto nomem;
	axel->url = u;

	for (i = 0; i < count; i++) {
		strlcpy(u[i].text, res[i].url, sizeof(u[i].text));
		u[i].next = &u[i + 1];
	}
	u[count - 1].next = u;

	axel->conn[0].conf = axel->conf;
	if (!conn_set(&axel->conn[0], axel->url->text)) {
		axel_message(axel, _("Could not parse URL.\n"));
		axel->ready = -1;
		return axel;
	}

	axel->conn[0].local_if = axel->conf->interfaces->text;
	axel->conf->interfaces = axel->conf->interfaces->next;

	strlcpy(axel->filename, axel->conn[0].file, sizeof(axel->filename));
	http_decode(axel->filename);

	if ((s = strchr(axel->filename, '?')) != NULL &&
	    axel->conf->strip_cgi_parameters)
		*s = 0;		/* Get rid of CGI parameters */

	if (*axel->filename == 0)	/* Index page == no fn */
		strlcpy(axel->filename, axel->conf->default_filename,
			sizeof(axel->filename));

	if (axel->conf->no_clobber && access(axel->filename, F_OK) == 0) {
		int ret = stfile_access(axel->filename, F_OK);
		if (ret) {
			printf(_("File '%s' already there; not retrieving.\n"),
			       axel->filename);
			axel->ready = -1;
			return axel;
		}
		printf(_("Incomplete download found, ignoring "
			 "no-clobber option\n"));
	}

	/* WSA startup for win32 */
	WSAINIT();

	do {
		if (!conn_init(&axel->conn[0])) {
			axel_message(axel, "%s", axel->conn[0].message);
			axel->ready = -1;
			return axel;
		}

		/* This does more than just checking the file size, it all
		 * depends on the protocol used. */
		status = conn_info(&axel->conn[0]);
		if (!status) {
			char msg[80];
			int code = conn_info_status_get(msg, sizeof(msg), axel->conn);
			fprintf(stderr, _("ERROR %d: %s.\n"), code, msg);
			axel->ready = -1;
			return axel;
		}
	} while (status == -1); /* re-init in case of protocol change. This can
				 * happen only once because the FTP protocol
				 * can't redirect back to HTTP */

	conn_url(axel->url->text, sizeof(axel->url->text) - 1, axel->conn);
	axel->size = axel->conn[0].size;
	if (axel->conf->verbose > 0) {
		if (axel->size != LLONG_MAX) {
			char hsize[32];
			axel_size_human(hsize, sizeof(hsize), axel->size);
			axel_message(axel, _("File size: %s (%jd bytes)"),
				     hsize, axel->size);
		} else {
			axel_message(axel, _("File size: unavailable"));
		}
	}

	/* Wildcards in URL --> Get complete filename */
	if (axel->filename[strcspn(axel->filename, "*?")])
		strlcpy(axel->filename, axel->conn[0].file,
			sizeof(axel->filename));

	if (*axel->conn[0].output_filename != 0) {
		strlcpy(axel->filename, axel->conn[0].output_filename,
			sizeof(axel->filename));
	}

	return axel;
 nomem:
	axel_close(axel);
	printf("%s\n", strerror(errno));
	return NULL;
}

/* Open a local file to store the downloaded data */
int
axel_open(axel_t *axel)
{
	int i, fd;
	ssize_t nread;

	if (axel->conf->verbose > 0)
		axel_message(axel, _("Opening output file %s"), axel->filename);

	axel->outfd = -1;

	/* Check whether server knows about RESTart and switch back to
	   single connection download if necessary */
	if (!axel->conn[0].supported) {
		axel_message(axel, _("Server unsupported, "
				     "starting from scratch with one connection."));
		axel->conf->num_connections = 1;
		void *new_conn = realloc(axel->conn, sizeof(conn_t));
		if (!new_conn)
			return 0;

		axel->conn = new_conn;
		axel_divide(axel);
	} else if ((fd = stfile_open(axel->filename, O_RDONLY, 0)) != -1) {
		int old_format = 0;
		off_t stsize = lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);

		nread = read(fd, &axel->conf->num_connections,
			     sizeof(axel->conf->num_connections));
		if (nread != sizeof(axel->conf->num_connections)) {
			printf(_("%s.st: Error, truncated state file\n"),
			       axel->filename);
			close(fd);
			return 0;
		}

		if (axel->conf->num_connections < 1) {
			fprintf(stderr,
				_("Bogus number of connections stored in state file\n"));
			close(fd);
			return 0;
		}

		if (stsize < (off_t)(sizeof(axel->conf->num_connections) +
				     sizeof(axel->bytes_done) +
				     2 * axel->conf->num_connections *
				     sizeof(axel->conn[0].currentbyte))) {
			/* FIXME this might be wrong, the file may have been
			 * truncated, we need another way to check. */
#ifndef NDEBUG
			printf(_("State file has old format.\n"));
#endif
			old_format = 1;
		}

		void *new_conn = realloc(axel->conn, sizeof(conn_t) *
					 axel->conf->num_connections);
		if (!new_conn) {
			close(fd);
			return 0;
		}
		axel->conn = new_conn;

		memset(axel->conn + 1, 0,
		       sizeof(conn_t) * (axel->conf->num_connections - 1));

		if (old_format)
			axel_divide(axel);

		nread = read(fd, &axel->bytes_done, sizeof(axel->bytes_done));
		assert(nread == sizeof(axel->bytes_done));
		for (i = 0; i < axel->conf->num_connections; i++) {
			nread = read(fd, &axel->conn[i].currentbyte,
				     sizeof(axel->conn[i].currentbyte));
			assert(nread == sizeof(axel->conn[i].currentbyte));
			if (!old_format) {
				nread = read(fd, &axel->conn[i].lastbyte,
					     sizeof(axel->conn[i].lastbyte));
				assert(nread == sizeof(axel->conn[i].lastbyte));
			}
		}

		axel_message(axel,
			     _("State file found: %jd bytes downloaded, %jd to go."),
			     axel->bytes_done, axel->size - axel->bytes_done);

		close(fd);

		if ((axel->outfd = open(axel->filename, O_WRONLY, 0666)) == -1) {
			axel_message(axel, _("Error opening local file"));
			return 0;
		}
	}

	/* If outfd == -1 we have to start from scrath now */
	if (axel->outfd == -1) {
		axel_divide(axel);

		if ((axel->outfd =
		     open(axel->filename, O_CREAT | O_WRONLY, 0666)) == -1) {
			axel_message(axel, _("Error opening local file"));
			return 0;
		}

		/* And check whether the filesystem can handle seeks to
		   past-EOF areas.. Speeds things up. :) AFAIK this
		   should just not happen: */
		if (lseek(axel->outfd, axel->size, SEEK_SET) == -1 &&
		    axel->conf->num_connections > 1) {
			/* But if the OS/fs does not allow to seek behind
			   EOF, we have to fill the file with zeroes before
			   starting. Slow.. */
			axel_message(axel,
				     _("Crappy filesystem/OS.. Working around. :-("));
			lseek(axel->outfd, 0, SEEK_SET);
			memset(buffer, 0, axel->conf->buffer_size);
			off_t j = axel->size;
			while (j > 0) {
				ssize_t nwrite;

				if ((nwrite =
				     write(axel->outfd, buffer,
					   min(j, axel->conf->buffer_size))) < 0) {
					if (errno == EINTR || errno == EAGAIN)
						continue;
					axel_message(axel,
						     _("Error creating local file"));
					return 0;
				}
				j -= nwrite;
			}
		}
	}

	return 1;
}

/**
 * Steals half of the largest available chunk of work of at least
 * MIN_CHUNK_WORTH size, from an active connection to feed a finished one.
 *
 * Must be called with the conn_t lock held.
 */
static
void
reactivate_connection(axel_t *axel, int thread)
{
	/* TODO Make the minimum also depend on the connection speed */
	off_t max_remaining = MIN_CHUNK_WORTH - 1;
	int idx = -1;

	if (axel->conn[thread].enabled ||
	    axel->conn[thread].currentbyte < axel->conn[thread].lastbyte)
		return;

	for (int j = 0; j < axel->conf->num_connections; j++) {
		off_t remaining =
			axel->conn[j].lastbyte - axel->conn[j].currentbyte;
		if (remaining > max_remaining) {
			max_remaining = remaining;
			idx = j;
		}
	}

	if (idx == -1)
		return;
#ifndef NDEBUG
	printf(_("\nReactivate connection %d\n"), thread);
#endif
	axel->conn[thread].lastbyte = axel->conn[idx].lastbyte;
	axel->conn[idx].lastbyte = axel->conn[idx].currentbyte
		+ max_remaining / 2;
	axel->conn[thread].currentbyte = axel->conn[idx].lastbyte;
}

/* Start downloading */
void
axel_start(axel_t *axel)
{
	int i;
	url_t *url_ptr;

	/* HTTP might've redirected and FTP handles wildcards, so
	   re-scan the URL for every conn */
	url_ptr = axel->url;
	for (i = 0; i < axel->conf->num_connections; i++) {
		conn_set(&axel->conn[i], url_ptr->text);
		url_ptr = url_ptr->next;
		axel->conn[i].local_if = axel->conf->interfaces->text;
		axel->conf->interfaces = axel->conf->interfaces->next;
		axel->conn[i].conf = axel->conf;
		if (i)
			axel->conn[i].supported = true;
	}

	if (axel->conf->verbose > 0)
		axel_message(axel, _("Starting download"));

	for (i = 0; i < axel->conf->num_connections; i++) {
		if (axel->conn[i].currentbyte >= axel->conn[i].lastbyte) {
			pthread_mutex_lock(&axel->conn[i].lock);
			reactivate_connection(axel, i);
			pthread_mutex_unlock(&axel->conn[i].lock);
		} else if (axel->conn[i].currentbyte < axel->conn[i].lastbyte) {
			if (axel->conf->verbose >= 2) {
				axel_message(axel,
					     _("Connection %i downloading from %s:%i using interface %s"),
					     i, axel->conn[i].host,
					     axel->conn[i].port,
					     axel->conn[i].local_if);
			}

			axel->conn[i].state = true;
			if (pthread_create
			    (axel->conn[i].setup_thread, NULL, setup_thread,
			     &axel->conn[i]) != 0) {
				axel_message(axel, _("pthread error!!!"));
				axel->ready = -1;
			}
		}
	}

	/* The real downloading will start now, so let's start counting */
	axel->start_time = axel_gettime();
	axel->ready = 0;
}

/* Main 'loop' */
void
axel_do(axel_t *axel)
{
	fd_set fds[1];
	int hifd, i;
	off_t remaining, size;
	struct timeval timeval[1];
	url_t *url_ptr;
	struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000};
	unsigned long long int max_speed_ratio;

	/* Create statefile if necessary */
	if (axel_gettime() > axel->next_state) {
		save_state(axel);
		axel->next_state = axel_gettime() + axel->conf->save_state_interval;
	}

	/* Wait for data on (one of) the connections */
	FD_ZERO(fds);
	hifd = 0;
	for (i = 0; i < axel->conf->num_connections; i++) {
		/* skip connection if setup thread hasn't released the lock yet */
		if (!pthread_mutex_trylock(&axel->conn[i].lock)) {
			if (axel->conn[i].enabled) {
				FD_SET(axel->conn[i].tcp->fd, fds);
				hifd = max(hifd, axel->conn[i].tcp->fd);
			}
			pthread_mutex_unlock(&axel->conn[i].lock);
		}
	}
	if (hifd == 0) {
		/* No connections yet. Wait... */
		if (axel_sleep(delay) < 0) {
			axel_message(axel,
				     _("Error while waiting for connection: %s"),
				     strerror(errno));
			axel->ready = -1;
			return;
		}
		goto conn_check;
	}

	timeval->tv_sec = 0;
	timeval->tv_usec = 100000;
	if (select(hifd + 1, fds, NULL, NULL, timeval) == -1) {
		/* A select() error probably means it was interrupted
		 * by a signal, or that something else's very wrong... */
		axel->ready = -1;
		return;
	}

	/* Handle connections which need attention */
	for (i = 0; i < axel->conf->num_connections; i++) {
		/* skip connection if setup thread hasn't released the lock yet */
		if (pthread_mutex_trylock(&axel->conn[i].lock))
			continue;

		if (!axel->conn[i].enabled)
			goto next_conn;

		if (!FD_ISSET(axel->conn[i].tcp->fd, fds)) {
			time_t timeout = axel->conn[i].last_transfer +
			    axel->conf->connection_timeout;
			if (axel_gettime() > timeout) {
				if (axel->conf->verbose)
					axel_message(axel,
						     _("Connection %i timed out"),
						     i);
				conn_disconnect(&axel->conn[i]);
			}
			goto next_conn;
		}

		axel->conn[i].last_transfer = axel_gettime();
		size =
		    tcp_read(axel->conn[i].tcp, buffer,
			     axel->conf->buffer_size);
		if (size == -1) {
			if (axel->conf->verbose) {
				axel_message(axel, _("Error on connection %i! "
						     "Connection closed"), i);
			}
			conn_disconnect(&axel->conn[i]);
			goto next_conn;
		}

		if (size == 0) {
			if (axel->conf->verbose) {
				/* Only abnormal behaviour if: */
				if (axel->conn[i].currentbyte <
				    axel->conn[i].lastbyte &&
				    axel->size != LLONG_MAX) {
					axel_message(axel,
						     _("Connection %i unexpectedly closed"),
						     i);
				} else {
					axel_message(axel,
						     _("Connection %i finished"),
						     i);
				}
			}
			if (!axel->conn[0].supported) {
				axel->ready = 1;
			}
			conn_disconnect(&axel->conn[i]);
			reactivate_connection(axel, i);
			goto next_conn;
		}

		/* remaining == Bytes to go */
		remaining = axel->conn[i].lastbyte - axel->conn[i].currentbyte;
		if (remaining < size) {
			if (axel->conf->verbose) {
				axel_message(axel, _("Connection %i finished"),
					     i);
			}
			conn_disconnect(&axel->conn[i]);
			size = remaining;
			/* Don't terminate, still stuff to write! */
		}
		/* This should always succeed.. */
		lseek(axel->outfd, axel->conn[i].currentbyte, SEEK_SET);
		if (write(axel->outfd, buffer, size) != size) {
			axel_message(axel, _("Write error!"));
			axel->ready = -1;
			pthread_mutex_unlock(&axel->conn[i].lock);
			return;
		}
		axel->conn[i].currentbyte += size;
		axel->bytes_done += size;
		if (remaining == size)
			reactivate_connection(axel, i);

 next_conn:
		pthread_mutex_unlock(&axel->conn[i].lock);
	}

	if (axel->ready)
		return;

 conn_check:
	/* Look for aborted connections and attempt to restart them. */
	url_ptr = axel->url;
	for (i = 0; i < axel->conf->num_connections; i++) {
		/* skip connection if setup thread hasn't released the lock yet */
		if (pthread_mutex_trylock(&axel->conn[i].lock))
			continue;

		if (!axel->conn[i].enabled &&
		    axel->conn[i].currentbyte < axel->conn[i].lastbyte) {
			if (!axel->conn[i].state) {
				// Wait for termination of this thread
				pthread_join(*(axel->conn[i].setup_thread),
					     NULL);

				conn_set(&axel->conn[i], url_ptr->text);
				url_ptr = url_ptr->next;
				/* axel->conn[i].local_if = axel->conf->interfaces->text;
				   axel->conf->interfaces = axel->conf->interfaces->next; */
				if (axel->conf->verbose >= 2)
					axel_message(axel,
						     _("Connection %i downloading from %s:%i using interface %s"),
						     i, axel->conn[i].host,
						     axel->conn[i].port,
						     axel->conn[i].local_if);

				axel->conn[i].state = true;
				if (pthread_create
				    (axel->conn[i].setup_thread, NULL,
				     setup_thread, &axel->conn[i]) == 0) {
					axel->conn[i].last_transfer = axel_gettime();
				} else {
					axel_message(axel,
						     _("pthread error!!!"));
					axel->ready = -1;
				}
			} else {
				if (axel_gettime() > (axel->conn[i].last_transfer +
						 axel->conf->reconnect_delay)) {
					pthread_cancel(*axel->conn[i].setup_thread);
					axel->conn[i].state = false;
					pthread_join(*axel->conn[i].
						     setup_thread, NULL);
				}
			}
		}
		pthread_mutex_unlock(&axel->conn[i].lock);
	}

	/* Calculate current average speed and finish_time */
	axel->bytes_per_second =
	    (off_t)((double)(axel->bytes_done - axel->start_byte) /
		  (axel_gettime() - axel->start_time));
	if (axel->bytes_per_second != 0)
		axel->finish_time =
		    (int)(axel->start_time +
			  (double)(axel->size - axel->start_byte) /
			  axel->bytes_per_second);
	else
		axel->finish_time = INT_MAX;

	/* Check speed. If too high, delay for some time to slow things
	   down a bit. I think a 5% deviation should be acceptable. */
	if (axel->conf->max_speed > 0) {
		max_speed_ratio = 1000 * axel->bytes_per_second /
		    axel->conf->max_speed;
		if (max_speed_ratio > 1050) {
			axel->delay_time.tv_nsec += 10000000;
			if (axel->delay_time.tv_nsec >= 1000000000) {
				axel->delay_time.tv_sec++;
				axel->delay_time.tv_nsec -= 1000000000;
			}
		} else if (max_speed_ratio < 950) {
			if (axel->delay_time.tv_nsec >= 10000000) {
				axel->delay_time.tv_nsec -= 10000000;
			} else if (axel->delay_time.tv_sec > 0) {
				axel->delay_time.tv_sec--;
				axel->delay_time.tv_nsec += 999000000;
			} else {
				axel->delay_time.tv_sec = 0;
				axel->delay_time.tv_nsec = 0;
			}
		}
		if (axel_sleep(axel->delay_time) < 0) {
			axel_message(axel,
				     _("Error while enforcing throttling: %s"),
				     strerror(errno));
			axel->ready = -1;
			return;
		}
	}

	/* Ready? */
	if (axel->bytes_done == axel->size)
		axel->ready = 1;
}

/* Close an axel connection */
void
axel_close(axel_t *axel)
{
	if (!axel)
		return;

	/* this function can't be called with a partly initialized axel */
	assert(axel->conn);

	/* WSA cleanup for win32 */
	WSAClose();

	/* Terminate threads and close connections */
	for (int i = 0; i < axel->conf->num_connections; i++) {
		/* don't try to kill non existing thread */
		if (*axel->conn[i].setup_thread != 0) {
			pthread_cancel(*axel->conn[i].setup_thread);
			pthread_join(*axel->conn[i].setup_thread, NULL);
		}
		conn_disconnect(&axel->conn[i]);
	}

	free(axel->url);

	/* Delete state file if necessary */
	if (axel->ready == 1) {
		stfile_unlink(axel->filename);
	}
	/* Else: Create it.. */
	else if (axel->bytes_done > 0) {
		save_state(axel);
	}

	print_messages(axel);

	close(axel->outfd);

	if (!PROTO_IS_FTP(axel->conn->proto) || axel->conn->proxy) {
		abuf_setup(axel->conn->http->request, ABUF_FREE);
		abuf_setup(axel->conn->http->headers, ABUF_FREE);
	}
	free(axel->conn);
	free(axel);
	free(buffer);
}

/* time() with more precision */
double
axel_gettime(void)
{
	struct timeval time[1];

	gettimeofday(time, NULL);
	return (double)time->tv_sec + (double)time->tv_usec / 1000000;
}

/**
 * Save the state of the current download.
 */
static
void
save_state(axel_t *axel)
{
	/* No use for such a file if the server doesn't support
	   resuming anyway.. */
	if (!axel->conn[0].supported)
		return;

	int fd;
	fd = stfile_open(axel->filename, O_CREAT | O_TRUNC | O_WRONLY, 0666);
	if (fd == -1) {
		return;		/* Not 100% fatal.. */
	}

	ssize_t nwrite;
	(void)nwrite; /* workaround unused variable warning */
	nwrite =
	    write(fd, &axel->conf->num_connections,
		  sizeof(axel->conf->num_connections));
	assert(nwrite == sizeof(axel->conf->num_connections));

	nwrite = write(fd, &axel->bytes_done, sizeof(axel->bytes_done));
	assert(nwrite == sizeof(axel->bytes_done));

	for (int i = 0; i < axel->conf->num_connections; i++) {
		nwrite =
		    write(fd, &axel->conn[i].currentbyte,
			  sizeof(axel->conn[i].currentbyte));
		assert(nwrite == sizeof(axel->conn[i].currentbyte));
		nwrite =
		    write(fd, &axel->conn[i].lastbyte,
			  sizeof(axel->conn[i].lastbyte));
		assert(nwrite == sizeof(axel->conn[i].lastbyte));
	}
	close(fd);
}

/* Thread used to set up a connection */
static
void *
setup_thread(void *c)
{
	conn_t *conn = c;
	int oldstate;

	/* Allow this thread to be killed at any time. */
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldstate);

	pthread_mutex_lock(&conn->lock);
	if (conn_setup(conn)) {
		conn->last_transfer = axel_gettime();
		if (conn_exec(conn)) {
			conn->last_transfer = axel_gettime();
			conn->enabled = true;
			goto out;
		}
	}

	conn_disconnect(conn);
 out:
	conn->state = false;
	pthread_mutex_unlock(&conn->lock);

	return NULL;
}

/* Add a message to the axel->message structure */
static void
axel_message(axel_t *axel, const char *format, ...)
{
	message_t *m;
	va_list params;

	if (!axel)
		goto nomem;

	m = calloc(1, sizeof(message_t));
	if (!m)
		goto nomem;

	va_start(params, format);
	vsnprintf(m->text, MAX_STRING, format, params);
	va_end(params);

	if (axel->message == NULL) {
		axel->message = axel->last_message = m;
	} else {
		axel->last_message->next = m;
		axel->last_message = m;
	}

	return;

 nomem:
	/* Flush previous messages */
	print_messages(axel);
	va_start(params, format);
	vprintf(format, params);
	va_end(params);
}

/* Divide the file and set the locations for each connection */
static void
axel_divide(axel_t *axel)
{
	/* Optimize the number of connections in case the file is small */
	off_t maxconns = max(1u, axel->size / MIN_CHUNK_WORTH);
	if (maxconns < axel->conf->num_connections)
		axel->conf->num_connections = maxconns;

	/* Calculate each segment's size */
	off_t seg_len = axel->size / axel->conf->num_connections;

	if (!seg_len) {
		printf(_("Too few bytes remaining, forcing a single connection\n"));
		axel->conf->num_connections = 1;
		seg_len = axel->size;

		conn_t *new_conn = realloc(axel->conn, sizeof(*axel->conn));
		if (new_conn)
			axel->conn = new_conn;
	}

	for (int i = 0; i < axel->conf->num_connections; i++) {
		axel->conn[i].currentbyte = seg_len * i;
		axel->conn[i].lastbyte    = seg_len * i + seg_len;
	}

	/* Last connection downloads remaining bytes */
	size_t tail = axel->size % seg_len;
	axel->conn[axel->conf->num_connections - 1].lastbyte += tail;
#ifndef NDEBUG
	for (int i = 0; i < axel->conf->num_connections; i++) {
		printf(_("Downloading %jd-%jd using conn. %i\n"),
		       axel->conn[i].currentbyte,
		       axel->conn[i].lastbyte, i);
	}
#endif
}
