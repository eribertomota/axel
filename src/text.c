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
#ifndef _WIN32
#include <sys/ioctl.h>
#endif
#include "axel.h"


static void stop(int signal);
static char *time_human(char *dst, size_t len, unsigned int value);
static void print_commas(off_t bytes_done);
static void print_alternate_output(axel_t *axel);
static void print_progress(off_t cur, off_t prev, off_t total, double kbps);
static void print_help(void);
static void print_version(void);
static void print_version_info(void);
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
	search_t *search;
	conf_t conf[1];
	axel_t *axel;
	int j, ret = 1;
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
			if(!(conf->add_header_count<MAX_ADD_HEADERS)) {
				fprintf(stderr,
					_("Too many custom headers (-H)! Currently only %u custom headers can be appended.\n"), MAX_ADD_HEADERS-HDR_count_init);
				goto free_conf;
			}
			strlcpy(conf->add_header[conf->add_header_count++], optarg,
				sizeof(conf->add_header[0]));
			break;
		case 's':
			if (!sscanf(optarg, "%llu", &conf->max_speed)) {
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
			print_version_info();
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

	if (argc - optind == 0) {
		print_help();
		goto free_conf;
	} else if (strcmp(argv[optind], "-") == 0) {
		s = malloc(MAX_STRING);
		if (!s)
			goto free_conf;

		if (scanf("%1024[^\n]s", s) != 1) {
			fprintf(stderr,
				_("Error when trying to read URL (Too long?).\n"));
			free(s);
			goto free_conf;
		}
	} else {
		s = argv[optind];
		if (strlen(s) > MAX_STRING) {
			fprintf(stderr,
				_("Can't handle URLs of length over %zu\n"),
				MAX_STRING);
			goto free_conf;
		}
	}

	printf(_("Initializing download: %s\n"), s);
	if (do_search) {
		search = calloc(conf->search_amount + 1, sizeof(search_t));
		if (!search)
			goto free_conf;

		search[0].conf = conf;
		if (conf->verbose)
			printf(_("Doing search...\n"));
		int i = search_makelist(search, s);
		if (i < 0) {
			fprintf(stderr, _("File not found\n"));
			goto free_conf;
		}
		if (conf->verbose)
			printf(_("Testing speeds, this can take a while...\n"));
		j = search_getspeeds(search, i);
		if (j < 0) {
			fprintf(stderr, _("Speed testing failed\n"));
			return 1;
		}

		search_sortlist(search, i);
		if (conf->verbose) {
			printf(_("%i usable servers found, will use these URLs:\n"),
			       j);
			j = min(j, conf->search_top);
			printf("%-60s %15s\n", "URL", _("Speed"));
			for (i = 0; i < j; i++)
				printf("%-70.70s %5jd\n", search[i].url,
				       search[i].speed);
			printf("\n");
		}
		axel = axel_new(conf, j, search);
		free(search);
		if (!axel || axel->ready == -1) {
			print_messages(axel);
			goto close_axel;
		}
	} else {
		search = calloc(argc - optind, sizeof(search_t));
		if (!search)
			goto free_conf;

		for (int i = 0; i < argc - optind; i++)
			strlcpy(search[i].url, argv[optind + i],
				sizeof(search[i].url));
		axel = axel_new(conf, argc - optind, search);
		free(search);
		if (!axel || axel->ready == -1) {
			print_messages(axel);
			goto close_axel;
		}
	}
	print_messages(axel);
	if (s != argv[optind]) {
		free(s);
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
		off_t prev;

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
				fputs("\e[2K\r", stdout);
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
	axel_size_human(hsize, sizeof(hsize), axel->bytes_done - axel->start_byte);

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
RETSIGTYPE
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
axel_size_human(char *dst, size_t len, size_t value)
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
print_commas(off_t bytes_done)
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
print_progress(off_t cur, off_t prev, off_t total, double kbps)
{
	prev /= 1024;
	cur /= 1024;

	bool print_speed = prev > 0;
	for (off_t i = prev; i < cur; i++) {
		if (i % 50 == 0) {
			if (print_speed)
				printf("  [%6.1fKB/s]", kbps);

			if (total == LLONG_MAX)
				printf("\n[ N/A]  ");
			else
				printf("\n[%3jd%%]  ",
				       min(100U, 102400 * i / total));
		} else if (i % 10 == 0) {
			putchar(' ');
		}
		putchar('.');
	}
}

static
char
alt_id(int n)
{
	const char *p = "09AZaz";
	while (*p && n > p[1] - p[0]) {
		n -= p[1] - p[0] + 1;
		p += 2;
	}
	return *p ? *p + n : '*';
}

static void
print_alternate_output_progress(axel_t *axel, char *progress, int width,
				off_t done, off_t total,
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
				progress[offset] = alt_id(i);
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
	off_t done = axel->bytes_done;
	off_t total = axel->size;
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

#ifdef _WIN32
static int
get_term_width()
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	return csbi.srWindow.Right - csbi.srWindow.Left + 1;
}
#else
static int
get_term_width(void)
{
	struct winsize w;

	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	return w.ws_col;
}
#endif

void
print_help(void)
{
	print_version();
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
	printf(_("Axel %s (%s)\n"), VERSION, ARCH);
}

void
print_version_info(void)
{
	print_version();
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
