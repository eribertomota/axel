/*
  Axel -- A lighter download accelerator for Linux and other Unices

  Copyright 2001-2007 Wilmer van der Gaast
  Copyright 2008      Y Giridhar Appaji Nag
  Copyright 2008-2010 Philipp Hagemeister
  Copyright 2015-2017 Joao Eriberto Mota Filho
  Copyright 2016      Denis Denisov
  Copyright 2016      Mridul Malpotra
  Copyright 2016      Stephen Thirlwall
  Copyright 2017      Antonio Quartulli
  Copyright 2017-2019 Ismael Luceno
  Copyright 2019      Evangelos Foutras
  Copyright 2019      Kun Ma


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

/* Text interface */

#include "config.h"
#include <sys/ioctl.h>
#include "axel.h"


static void stop(int signal);
static char *size_human(char *dst, size_t len, size_t value);
static char *time_human(char *dst, size_t len, unsigned int value);
static void print_commas(long long int bytes_done);
static void print_alternate_output(axel_t *axel);
static void print_progress(size_t cur, size_t prev, size_t total, double kbps);
static void print_help(void);
static void print_version(void);
static int get_term_width(void);

int run = 1;

#define MAX_REDIR_OPT	256

#ifdef NOGETOPTLONG
#define getopt_long(a, b, c, d, e) getopt(a, b, c)
#else
static struct option axel_options[] = {
	/* name             has_arg flag  val */
	{"max-speed",       1,      NULL, 's'},
	{"num-connections", 1,      NULL, 'n'},
	{"max-redirect",    1,      NULL, MAX_REDIR_OPT},
	{"output",          1,      NULL, 'o'},
	{"search",          2,      NULL, 'S'},
	{"ipv4",            0,      NULL, '4'},
	{"ipv6",            0,      NULL, '6'},
	{"no-proxy",        0,      NULL, 'N'},
	{"quiet",           0,      NULL, 'q'},
	{"verbose",         0,      NULL, 'v'},
	{"help",            0,      NULL, 'h'},
	{"version",         0,      NULL, 'V'},
	{"alternate",       0,      NULL, 'a'},
	{"insecure",        0,      NULL, 'k'},
	{"no-clobber",      0,      NULL, 'c'},
	{"header",          1,      NULL, 'H'},
	{"user-agent",      1,      NULL, 'U'},
	{"timeout",         1,      NULL, 'T'},
	{NULL,              0,      NULL, 0}
};
#endif

int
main(int argc, char *argv[])
{
	char fn[MAX_STRING];
	int do_search = 0;
	search_t *search, *list;
	search_t *tmp, *cur;
	conf_t conf[1];
	axel_t *axel;
	int prealloc_num, url_num = 0;
	int j, cur_head = 0, ret = 1;
	char *s;

	fn[0] = 0;

/* Set up internationalization (i18n) */
#ifdef ENABLE_NLS
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
#endif

	if (!conf_init(conf)) {
		return 1;
	}

	opterr = 0;

	j = -1;
	while (1) {
		int option = getopt_long(argc, argv,
					 "s:n:o:S::46NqvhVakcH:U:T:",
					 axel_options, NULL);
		if (option == -1)
			break;

		switch (option) {
		case 'U':
			conf_hdr_make(conf->add_header[HDR_USER_AGENT],
				      "User-Agent", optarg);
			break;
		case 'H':
			strlcpy(conf->add_header[cur_head++], optarg,
				sizeof(conf->add_header[0]));
			break;
		case 's':
			if (!sscanf(optarg, "%i", &conf->max_speed)) {
				print_help();
				goto free_conf;
			}
			break;
		case 'n':
			if (!sscanf(optarg, "%hu", &conf->num_connections)) {
				print_help();
				goto free_conf;
			}
			break;
		case MAX_REDIR_OPT:
			if (!sscanf(optarg, "%i", &conf->max_redirect)) {
				print_help();
				return 1;
			}
			break;
		case 'o':
			strlcpy(fn, optarg, sizeof(fn));
			break;
		case 'S':
			do_search = 1;
			if (optarg) {
				if (!sscanf(optarg, "%i", &conf->search_top)) {
					print_help();
					goto free_conf;
				}
			}
			break;
		case '6':
			conf->ai_family = AF_INET6;
			break;
		case '4':
			conf->ai_family = AF_INET;
			break;
		case 'a':
			conf->alternate_output = 1;
			break;
		case 'k':
			conf->insecure = 1;
			break;
		case 'c':
			conf->no_clobber = 1;
			break;
		case 'N':
			*conf->http_proxy = 0;
			break;
		case 'h':
			print_help();
			ret = 0;
			goto free_conf;
		case 'v':
			if (j == -1)
				j = 1;
			else
				j++;
			break;
		case 'V':
			print_version();
			ret = 0;
			goto free_conf;
		case 'q':
			close(1);
			conf->verbose = -1;
			if (open("/dev/null", O_WRONLY) != 1) {
				fprintf(stderr,
					_("Can't redirect stdout to /dev/null.\n"));
				goto free_conf;
			}
			break;
		case 'T':
			conf->io_timeout = strtoul(optarg, NULL, 0);
			break;
		default:
			print_help();
			goto free_conf;
		}
	}
	conf->add_header_count = cur_head;

	/* disable alternate output and verbosity when quiet is specified */
	if (conf->verbose < 0)
		conf->alternate_output = 0;
	else if (j > -1)
		conf->verbose = j;

	if (conf->num_connections < 1) {
		print_help();
		goto free_conf;
	}

	if (conf->max_redirect < 0) {
		print_help();
		return 1;
	}
#ifdef HAVE_SSL
	ssl_init(conf);
#endif				/* HAVE_SSL */

	prealloc_num = url_num = argc - optind;
	if (url_num == 0) {
		print_help();
		goto free_conf;
	}

	list = calloc(prealloc_num, sizeof(search_t));
	if (!list) {
		fprintf(stderr, _("%s\n"), strerror(errno));
		goto free_conf;
	}

	argv += optind;
	/* if "-" is the only argument */
	if (prealloc_num == 1 && argv[0][0] == '-' && !argv[0][1]) {
		url_num = search_readlist(list, stdin);
		if (url_num == 0) {
			print_help();
			goto free_conf;
		}
		tmp = list;
		list = list->next;
		free(tmp);
	} else {
		/* load URLs from argv and stdin */
		tmp = cur = list;
		for (; *argv; argv++) {
			if (argv[0][0] == '-' && !argv[0][1]) {
				/* add URL list from stdin to end of list */
				/* "-" and the last node will be ignored */
				url_num += search_readlist(list + prealloc_num - 2, stdin) - 1;
				continue;
			}
			/* add URL from argv */
			strlcpy(tmp->url, *argv, sizeof(tmp->url));
			cur->next = tmp++;
			cur = cur->next;
		}
	}

	printf(_("Initializing download: %s\n"), list[0].url);
	if (do_search) {
		search = calloc(conf->search_amount + 1, sizeof(search_t));
		if (!search) {
			return 1;
		}

		search[0].conf = conf;
		if (conf->verbose)
			printf(_("Doing search...\n"));
		// FIXME duplicated URL list[0].url
		int i = search_makelist(search, list[0].url);
		if (i < 0) {
			fprintf(stderr, _("File not found\n"));
			return 1;
		}
		if (conf->verbose)
			printf(_("Testing speeds, this can take a while...\n"));

		// FIXME only URLs in 'search' are tested and sorted
		j = search_getspeeds(search, i);
		if (j < 0) {
			fprintf(stderr, _("Speed testing failed\n"));
			// FIXME may leak memory
			return 1;
		}

		search_sortlist(search, i);
		j = min(j, conf->search_top);
		/* add search results into URL list */
		search[j - 1].next = list;
		url_num += j;
		if (conf->verbose) {
			printf(_("%i usable servers found, will use these URLs:\n"),
			       j);
			printf("%-60s %15s\n", "URL", _("Speed"));
			for (i = 0; i < j; i++)
				printf("%-70.70s %5i\n", search[i].url,
				       search[i].speed);
			printf("\n");
		}
	} else {
		search = list;
	}

#ifdef DEBUG
	tmp = search;
	for (int i = 0; i < url_num; i++) {
		printf("URL loaded: %s\n", tmp->url);
		tmp = tmp->next;
	}
#endif

	axel = axel_new(conf, url_num, search);
	print_messages(axel);
	if (!axel || axel->ready == -1) {
		goto close_axel;
	}

	/* Check if a file name has been specified */
	if (*fn) {
		struct stat buf;

		if (stat(fn, &buf) == 0) {
			if (S_ISDIR(buf.st_mode)) {
				size_t fnlen = strlen(fn);
				size_t axelfnlen = strlen(axel->filename);

				if (fnlen + 1 + axelfnlen + 1 > MAX_STRING) {
					fprintf(stderr, _("Filename too long!\n"));
					goto close_axel;
				}

				fn[fnlen] = '/';
				memcpy(fn + fnlen + 1, axel->filename,
				       axelfnlen);
				fn[fnlen + 1 + axelfnlen] = '\0';
			}
		}
		char statefn[MAX_STRING + 3];
		snprintf(statefn, sizeof(statefn), "%s.st", fn);
		if (access(fn, F_OK) == 0 && access(statefn, F_OK) != 0) {
			fprintf(stderr, _("No state file, cannot resume!\n"));
			goto close_axel;
		}
		if (access(statefn, F_OK) == 0 && access(fn, F_OK) != 0) {
			printf(_("State file found, but no downloaded data. Starting from scratch.\n"));
			unlink(statefn);
		}
		strlcpy(axel->filename, fn, sizeof(axel->filename));
	} else {
		/* Local file existence check */
		s = axel->filename + strlen(axel->filename);
		for (int i = 0; 1; i++) {
			char statefn[MAX_STRING + 3];
			snprintf(statefn, sizeof(statefn), "%s.st",
				 axel->filename);

			int f_exists = !access(axel->filename, F_OK);
			int st_exists = !access(statefn, F_OK);
			if (f_exists) {
				if (axel->conn[0].supported && st_exists)
						break;
			} else if (!st_exists)
				break;
			snprintf(s, axel->filename + sizeof(axel->filename) - s,
				 ".%i", i);
		}
	}

	if (!axel_open(axel)) {
		print_messages(axel);
		goto close_axel;
	}
	print_messages(axel);
	axel_start(axel);
	print_messages(axel);

	if (conf->alternate_output) {
		putchar('\n');
	} else {
		if (axel->bytes_done > 0) {	/* Print first dots if resuming */
			putchar('\n');
			print_commas(axel->bytes_done);
			fflush(stdout);
		}
	}
	axel->start_byte = axel->bytes_done;

	/* Install save_state signal handler for resuming support */
	signal(SIGINT, stop);
	signal(SIGTERM, stop);

	while (!axel->ready && run) {
		size_t prev;

		prev = axel->bytes_done;
		axel_do(axel);

		if (conf->alternate_output) {
			if (!axel->message && prev != axel->bytes_done)
				print_alternate_output(axel);
		} else if (conf->verbose > -1) {
			print_progress(axel->bytes_done, prev, axel->size,
				       (double)axel->bytes_per_second / 1024);
		}

		if (axel->message) {
			if (conf->alternate_output == 1) {
				/* clreol-simulation */
				putchar('\r');
				for (int i = get_term_width(); i > 0; i--)
					putchar(' ');
				putchar('\r');
			} else {
				putchar('\n');
			}
			print_messages(axel);
			if (!axel->ready) {
				if (conf->alternate_output != 1)
					print_commas(axel->bytes_done);
				else
					print_alternate_output(axel);
			}
		} else if (axel->ready) {
			putchar('\n');
		}
		fflush(stdout);
	}

	char hsize[MAX_STRING / 2], htime[MAX_STRING / 2];
	time_human(htime, sizeof(htime), axel_gettime() - axel->start_time);
	size_human(hsize, sizeof(hsize), axel->bytes_done - axel->start_byte);

	printf(_("\nDownloaded %s in %s. (%.2f KB/s)\n"), hsize, htime,
	       (double)axel->bytes_per_second / 1024);

	ret = axel->ready ? 0 : 2;

 close_axel:
	axel_close(axel);
 free_conf:
	conf_free(conf);

	return ret;
}

/* SIGINT/SIGTERM handler */
void
stop(int signal)
{
	(void)signal;
	run = 0;
}

/**
 * Integer base-2 logarithm.
 */
static inline
unsigned
log2i(unsigned long long x)
{
	return x ? sizeof(x) * 8 - 1 - __builtin_clzll(x) : 0;
}

/* Convert a number of bytes to a human-readable form */
char *
size_human(char *dst, size_t len, size_t value)
{
	double fval = (double)value;
	const char * const oname[] = {
		"", _("Kilo"), _("Mega"), _("Giga"), _("Tera"),
	};
	const unsigned int order = min(sizeof(oname) / sizeof(oname[0]) - 1,
				       log2i(fval) / 10);

	fval /= (double)(1 << order * 10);
	int ret = snprintf(dst, len, _("%g %sbyte(s)"), fval, oname[order]);
	return ret < 0 ? NULL : dst;
}

/* Convert a number of seconds to a human-readable form */
char *
time_human(char *dst, size_t len, unsigned int value)
{
	unsigned int hh, mm, ss;

	ss = value % 60;
	mm = value / 60 % 60;
	hh = value / 3600;

	int ret;
	if (hh)
		ret = snprintf(dst, len, _("%i:%02i:%02i hour(s)"), hh, mm, ss);
	else if (mm)
		ret = snprintf(dst, len, _("%i:%02i minute(s)"), mm, ss);
	else
		ret = snprintf(dst, len, _("%i second(s)"), ss);

	return ret < 0 ? NULL : dst;
}

/* Part of the infamous wget-like interface. Just put it in a function
	because I need it quite often.. */
void
print_commas(long long int bytes_done)
{
	int i, j;

	printf("       ");
	j = (bytes_done / 1024) % 50;
	if (j == 0)
		j = 50;
	for (i = 0; i < j; i++) {
		if ((i % 10) == 0)
			putchar(' ');
		putchar(',');
	}
}


/**
 * The infamous wget-like 'interface'.. ;)
 */
static
void
print_progress(size_t cur, size_t prev, size_t total, double kbps)
{
	prev /= 1024;
	cur /= 1024;

	bool print_speed = prev > 0;
	for (size_t i = prev; i < cur; i++) {
		if (i % 50 == 0) {
			if (print_speed)
				printf("  [%6.1fKB/s]", kbps);

			if (total == LLONG_MAX)
				printf("\n[ N/A]  ");
			else
				printf("\n[%3zu%%]  ",
				       min(100U, 102400 * i / total));
		} else if (i % 10 == 0) {
			putchar(' ');
		}
		putchar('.');
	}
}


static void
print_alternate_output_progress(axel_t *axel, char *progress, int width,
				long long int done, long long int total,
				double now)
{
	if (!width)
		width = 1;
	if (!total)
		total = 1;
	for (int i = 0; i < axel->conf->num_connections; i++) {
		int offset = axel->conn[i].currentbyte * width / total;

		if (axel->conn[i].currentbyte < axel->conn[i].lastbyte) {
			if (now <= axel->conn[i].last_transfer
				   + axel->conf->connection_timeout / 2) {
				progress[offset] = i
					+ (i < 10 ? '0' : ('A' - 10));
			} else
				progress[offset] = '#';
		}
		memset(progress + offset + 1, ' ',
		       max(0, axel->conn[i].lastbyte * width / total - offset - 1));
	}

	progress[width] = '\0';
	printf("\r[%3ld%%] [%s", min(100, (long)(done * 100. / total + .5)),
	       progress);
}

static void
print_alternate_output(axel_t *axel)
{
	long long int done = axel->bytes_done;
	long long int total = axel->size;
	double now = axel_gettime();
	int width = get_term_width();
	char *progress;

	if (width < 40) {
		fprintf(stderr,
			_("Can't setup alternate output. Deactivating.\n"));
		axel->conf->alternate_output = 0;

		return;
	}

	width -= 30;
	progress = malloc(width + 1);
	if (!progress)
		return;

	memset(progress, '.', width);

	if (total != LLONG_MAX) {
		print_alternate_output_progress(axel, progress, width, done,
						total, now);
	} else {
		progress[width] = '\0';
		printf("\r[ N/A] [%s", progress);
	}

	if (axel->bytes_per_second > 1048576)
		printf("] [%6.1fMB/s]",
		       (double)axel->bytes_per_second / (1024 * 1024));
	else if (axel->bytes_per_second > 1024)
		printf("] [%6.1fKB/s]", (double)axel->bytes_per_second / 1024);
	else
		printf("] [%6.1fB/s]", (double)axel->bytes_per_second);

	if (total != LLONG_MAX && done < total) {
		int seconds, minutes, hours, days;
		seconds = axel->finish_time - now;
		minutes = seconds / 60;
		seconds -= minutes * 60;
		hours = minutes / 60;
		minutes -= hours * 60;
		days = hours / 24;
		hours -= days * 24;
		if (days)
			printf(" [%2dd%2d]", days, hours);
		else if (hours)
			printf(" [%2dh%02d]", hours, minutes);
		else
			printf(" [%02d:%02d]", minutes, seconds);
	}

	free(progress);
}

static int
get_term_width(void)
{
	struct winsize w;

	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	return w.ws_col;
}

void
print_help(void)
{
#ifdef NOGETOPTLONG
	printf(_("Usage: axel [options] url1 [url2] [url...]\n"
		 "\n"
		 "-s x\tSpecify maximum speed (bytes per second)\n"
		 "-n x\tSpecify maximum number of connections\n"
		 "-o f\tSpecify local output file\n"
		 "-S[n]\tSearch for mirrors and download from n servers\n"
		 "-4\tUse the IPv4 protocol\n"
		 "-6\tUse the IPv6 protocol\n"
		 "-H x\tAdd HTTP header string\n"
		 "-U x\tSet user agent\n"
		 "-N\tJust don't use any proxy server\n"
		 "-k\tDon't verify the SSL certificate\n"
		 "-c\tSkip download if file already exists\n"
		 "-q\tLeave stdout alone\n"
		 "-v\tMore status information\n"
		 "-a\tAlternate progress indicator\n"
		 "-h\tThis information\n"
		 "-T x\tSet I/O and connection timeout\n"
		 "-V\tVersion information\n"
		 "\n"
		 "Visit https://github.com/axel-download-accelerator/axel/issues\n"));
#else
	printf(_("Usage: axel [options] url1 [url2] [url...]\n"
		 "\n"
		 "--max-speed=x\t\t-s x\tSpecify maximum speed (bytes per second)\n"
		 "--num-connections=x\t-n x\tSpecify maximum number of connections\n"
		 "--max-redirect=x\t\tSpecify maximum number of redirections\n"
		 "--output=f\t\t-o f\tSpecify local output file\n"
		 "--search[=n]\t\t-S[n]\tSearch for mirrors and download from n servers\n"
		 "--ipv4\t\t\t-4\tUse the IPv4 protocol\n"
		 "--ipv6\t\t\t-6\tUse the IPv6 protocol\n"
		 "--header=x\t\t-H x\tAdd HTTP header string\n"
		 "--user-agent=x\t\t-U x\tSet user agent\n"
		 "--no-proxy\t\t-N\tJust don't use any proxy server\n"
		 "--insecure\t\t-k\tDon't verify the SSL certificate\n"
		 "--no-clobber\t\t-c\tSkip download if file already exists\n"
		 "--quiet\t\t\t-q\tLeave stdout alone\n"
		 "--verbose\t\t-v\tMore status information\n"
		 "--alternate\t\t-a\tAlternate progress indicator\n"
		 "--help\t\t\t-h\tThis information\n"
		 "--timeout=x\t\t-T x\tSet I/O and connection timeout\n"
		 "--version\t\t-V\tVersion information\n"
		 "\n"
		 "Visit https://github.com/axel-download-accelerator/axel/issues to report bugs\n"));
#endif
}

void
print_version(void)
{
	printf(_("Axel version %s (%s)\n"), VERSION, ARCH);
	printf("\nCopyright 2001-2007 Wilmer van der Gaast,\n"
	       "\t  2007-2009 Giridhar Appaji Nag,\n"
	       "\t  2008-2010 Philipp Hagemeister,\n"
	       "\t  2015-2017 Joao Eriberto Mota Filho,\n"
	       "\t  2016-2017 Stephen Thirlwall,\n"
	       "\t  2017      Ismael Luceno,\n"
	       "\t  2017      Antonio Quartulli,\n"
	       "\t\t    %s\n%s\n\n", _("and others."),
	       _("Please, see the CREDITS file.\n\n"));
}

/* Print any message in the axel structure */
void
print_messages(axel_t *axel)
{
	message_t *m;

	if (!axel)
		return;

	while ((m = axel->message)) {
		printf("%s\n", m->text);
		axel->message = m->next;
		free(m);
	}
}
