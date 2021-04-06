/*
  Axel -- A lighter download accelerator for Linux and other Unices

  Copyright 2001-2007 Wilmer van der Gaast
  Copyright 2008      Y Giridhar Appaji Nag
  Copyright 2010      Philipp Hagemeister
  Copyright 2016      Stephen Thirlwall
  Copyright 2017      Antonio Quartulli
  Copyright 2017-2019 Ismael Luceno
  Copyright 2017      Joao Eriberto Mota Filho

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

/* filesearching.com searcher */

#include "config.h"
#include "axel.h"
#include "sleep.h"

static void *search_speedtest(void *r);
static int search_sortlist_qsort(const void *a, const void *b);

#ifdef STANDALONE
int
main(int argc, char *argv[])
{
	conf_t conf[1];
	search_t *res;
	int i, j, num_mirrors, ret = 1;

	if (argc != 2) {
		fprintf(stderr, _("Incorrect amount of arguments\n"));
		return 1;
	}

	conf_init(conf);
	ssl_init(conf);

	res = calloc(conf->search_amount + 1, sizeof(search_t));
	if (!res)
		goto out;

	res->conf = conf;

	i = search_makelist(res, argv[1]);
	if (i == -1) {
		fprintf(stderr, _("File not found\n"));
		goto out;
	}
	num_mirrors = search_getspeeds(res, i);
	if (num_mirrors < 0) {
		fprintf(stderr, _("Speed testing failed\n"));
		goto out;
	}
	printf(_("%i usable mirrors:\n"), num_mirrors);
	search_sortlist(res, i);
	for (j = 0; j < i; j++)
		printf("%-70.70s %5i\n", res[j].url, res[j].speed);

	ret = 0;
out:
	free(res);

	return ret;
}
#endif

int
search_makelist(search_t *results, char *orig_url)
{
	int size = 8192;
	conn_t conn[1];
	double t;
	const char *start, *end;

	memset(conn, 0, sizeof(conn_t));

	conn->conf = results->conf;
	t = axel_gettime();
	if (!conn_set(conn, orig_url, true) || !conn_init(conn) || !conn_info(conn))
		return -1;

	size_t orig_len = strlcpy(results[0].url, orig_url,
				  sizeof(results[0].url));
	results[0].speed = 1 + 1000 * (axel_gettime() - t);
	results[0].size = conn->size;
	int nresults = 1;

	char *s = malloc(size);
	if (!s)
		return 1;

	/* TODO improve matches */
	snprintf(s, size, "http://www.filesearching.com/cgi-bin/s?"
		 "w=a&" /* TODO describe */
		 "x=15&y=15&" /* TODO describe */
		 /* Size in bytes:   */ "s=on&"
		 /* Exact search:    */ "e=on&"
		 /* Language:        */ "l=en&"
		 /* Search Type:     */ "t=f&"
		 /* Sorting:         */ "o=n&"
		 /* Filename:        */ "q=%s&"
		 /* Num. of results: */ "m=%i&"
		 /* Size (min/max):  */ "s1=%jd&s2=%jd",
		 conn->file, results->conf->search_amount,
		 conn->size, conn->size);

	conn_disconnect(conn);
	memset(conn, 0, sizeof(conn_t));
	conn->conf = results->conf;

	if (!conn_set(conn, s, true))
		goto done;

	{
		pthread_mutex_unlock(&conn->lock);
		int tmp = conn_setup(conn);
		pthread_mutex_unlock(&conn->lock);
		if (!tmp || !conn_exec(conn))
			goto done;
	}

	{
		int j = 0;

		for (int i; (i = tcp_read(conn->tcp, s + j, size - j)) > 0;) {
			j += i;
			if (j + 10 >= size) {
				size *= 2;
				char *tmp = realloc(s, size);
				if (!tmp)
					goto done;
				s = tmp;
				memset(s + size / 2, 0, size / 2);
			}
		}
		s[j] = '\0';
	}

	conn_disconnect(conn);

	start = strstr(s, "<pre class=list");
	if (!start)
		goto done;
	end = strstr(start, "</pre>");
	/* Incomplete list */
	if (!end)
		goto done;

	for (const char *url, *eol;
	     start < end && nresults < results->conf->search_amount;
	     start = eol + 1) {
		eol = strchr(start, '\n');
		if (eol > end || !eol)
			eol = end;
		do {
			url = start;
			start = strstr(start, "<a href=") + 8;
		} while (start < eol);

		/* Check it isn't the original URL */
		if (!strncmp(url, orig_url, orig_len))
			continue;

		strlcpy(results[nresults].url, url, sizeof(results[0].url));
		results[nresults].size = results[0].size;
		results[nresults].conf = results->conf;
		++nresults;
	}

done:
	free(s);
	return nresults;
}

enum {
	SPEED_ACTIVE  = -3,
	SPEED_FAILED  = -2,
	SPEED_DONE    = -1,
	SPEED_PENDING =  0,
	/* Or > 0 */
};

int
search_getspeeds(search_t *results, int count)
{
	const struct timespec delay = {.tv_nsec = 10000000};

	int left = count, correct = 0;
	for (int i = 0; i < count; i++) {
		if (results[i].speed) {
			results[i].speed_start_time = 0;
			left--;
			if (results[i].speed > 0)
				correct++;
		}
	}

	for (int running = 0; left > 0; axel_sleep(delay)) {
		for (int i = 0; i < count; i++) {
			switch (results[i].speed) {
			case SPEED_ACTIVE:
				if (axel_gettime() < results[i].speed_start_time
				    + results->conf->search_timeout)
					continue; // not timed out yet
				pthread_cancel(*results[i].speed_thread);
				break; // do the bookkeeping
			case SPEED_FAILED:
				break; // do the bookkeeping
			case SPEED_DONE:
				continue; // already processed
			case SPEED_PENDING:
				if (running >= results->conf->search_threads)
					continue; // running too many, skip
				results[i].speed = SPEED_ACTIVE;
				results[i].speed_start_time = axel_gettime();
				if (pthread_create(results[i].speed_thread,
						   NULL, search_speedtest,
						   &results[i]) == 0)
					running++;
				else
					results[i].speed = SPEED_PENDING;
				continue; // go to the next item
			default:
				if (!results[i].speed_start_time)
					continue; // already processed
				// do the bookkeeping
			}
			// The thread must have been cancelled or finished
			pthread_join(*results[i].speed_thread, NULL);
			running--;
			left--;
			switch (results[i].speed) {
			case SPEED_ACTIVE:
			case SPEED_FAILED:
				results[i].speed = SPEED_DONE;
				break;
			default:
				results[i].speed_start_time = 0;
				if (results[i].speed > 0)
					correct++;
			}
		}
	}

	return correct;
}

static
void *
search_speedtest(void *r)
{
	search_t *results = r;
	conn_t conn[1];
	int oldstate;

	/* Allow this thread to be killed at any time. */
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldstate);

	memset(conn, 0, sizeof(conn_t));
	conn->conf = results->conf;
	if (conn_set(conn, results->url, true)
	    && conn_init(conn)
	    && conn_info(conn)
	    && conn->size == results->size)
		/* Add one because it mustn't be zero */
		results->speed = 1 + 1000 * (axel_gettime() - results->speed_start_time);
	else
		results->speed = SPEED_FAILED;

	conn_disconnect(conn);

	return NULL;
}


void
search_sortlist(search_t *results, int count)
{
	qsort(results, count, sizeof(search_t), search_sortlist_qsort);
}

static
int
search_sortlist_qsort(const void *a, const void *b)
{
	if (((search_t *) a)->speed < 0 && ((search_t *) b)->speed > 0)
		return 1;
	if (((search_t *) a)->speed > 0 && ((search_t *) b)->speed < 0)
		return -1;
	if (((search_t *) a)->speed < ((search_t *) b)->speed)
		return -1;
	else
		return ((search_t *) a)->speed > ((search_t *) b)->speed;
}
