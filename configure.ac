# Autoconf for axel
AC_COPYRIGHT([
Copyright 2016      Joao Eriberto Mota Filho
Copyright 2016      Stephen Thirlwall
Copyright 2017      Antonio Quartulli
Copyright 2017-2020 Ismael Luceno
Copyright 2017      Vlad Glagolev

This file is under the same license as Axel.
])

AC_PREREQ([2.69])
PKG_PREREQ([0.29])

AXEL_PKG([axel], [https://github.com/axel-download-accelerator/axel/issues])
AXEL_VER_READ([VERSION])
AC_INIT

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([src/conf.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_LIBOBJ_DIR([lib])
AM_INIT_AUTOMAKE([subdir-objects])
PKG_PROG_PKG_CONFIG
AS_IF([test -z "$PKG_CONFIG"],
  [AC_MSG_WARN([pkg-config is needed for auto-configuration of dependencies])
   PKG_CONFIG=false])

AM_SILENT_RULES([yes])

# OS-specific stuff
AC_CANONICAL_HOST
AC_DEFINE_UNQUOTED([ARCH], ["$host_os"], ["Define architecture"])

# Make system extensions available to the code
AS_CASE(["$host_os"],
  [darwin*], [
    AC_DEFINE([_DARWIN_C_SOURCE], [1],
              [Define on Darwin to expose all libc features])
  ],
  [solaris*], [
    LIBS="$LIBS -lsocket -lnsl"
  ],
  [hpux*], [
    AS_IF([test -d /usr/local/lib/hpux32], [
      LDFLAGS="$LDFLAGS -L/usr/local/lib/hpux32"
    ])
  ],
  [*mingw32], [
    AC_DEFINE([_WIN32_WINNT], [0x0600],
              [Request NT 6 API to expose AI_ADDRCONFIG in WinSock2])
  ])
    AC_DEFINE([WIN32_LEAN_AND_MEAN], [],
              [Avoid some OpenSSL macros redefined by MinGW])

    LIBS="$LIBS -lws2_32"
    PTHREAD_CFLAGS="-static $PTHREAD_CFLAGS"
    AS_IF([test "x$with_ssl" = xno], [], [
        CFLAGS="$CFLAGS $SSL_CFLAGS"
    ])
AC_USE_SYSTEM_EXTENSIONS

# Compiler checks
AC_PROG_CC
AC_PROG_CC_C99
AS_IF([test "x$ac_cv_prog_cc_c99" = "xno"], [
    AC_MSG_ERROR([C99 compiler required])
])

# Large file support
AC_SYS_LARGEFILE

# Compiler Warnings
AX_IS_RELEASE([git-directory])
AX_COMPILER_FLAGS()

# Debugging code
AC_ARG_ENABLE(debug,
    AS_HELP_STRING([--enable-debug], [enable debugging, default: no]))
AS_IF([! test "x$enable_debug" = xyes ], [
    AC_DEFINE([NDEBUG], [1], [disable debugging code])
])

# GCC function attributes
AX_GCC_FUNC_ATTRIBUTE([format])

# Built-in functions
AX_GCC_BUILTIN([__builtin_clzll])

AS_IF([test "x$ax_cv_have___builtin_clzll" = xno], [
    AC_MSG_ERROR([Support for __builtin_clzll is required])
])


# Checks for other programs.
AC_PROG_INSTALL

# Checks for libraries.

# Checks for header files.
AS_IF([test "x$host_os" = "x${host_os%mingw32}"], [
  AC_CHECK_HEADERS([ \
    arpa/inet.h \
    fcntl.h \
    limits.h \
    locale.h \
    netdb.h \
    netinet/in.h \
    stdlib.h \
    sys/ioctl.h \
    sys/socket.h \
    sys/time.h \
    unistd.h \
  ],, [
      AC_MSG_ERROR([$ac_header is required.])
  ])
])
# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE

# Ensure optional C99 integer types we need
AC_TYPE_INT8_T
AC_TYPE_UINT8_T
AC_TYPE_INT16_T
AC_TYPE_UINT16_T
AC_TYPE_INT32_T
AC_TYPE_UINT32_T
AC_TYPE_INT64_T
AC_TYPE_UINT64_T

# POSIX types
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_MODE_T
AC_TYPE_SIGNAL

# Custom type checks and replacements
AXEL_CHECK_TYPE([off_t], [sizeof(off_t) >= sizeof(int64_t)])
AXEL_DEFAULT_TYPE([off_t], [int64_t])

# OS-specific issues
AS_CASE(["$host_os"],
  [darwin*], [
    AXEL_CHECK_TYPE([intmax_t], [sizeof(intmax_t) >= sizeof(int64_t)])
  ])

# Checks for library functions.
AC_CHECK_FUNCS([ \
	getaddrinfo \
	gettimeofday \
	inet_ntoa \
	malloc \
	memset \
	nanosleep \
	realloc \
	select \
	setlocale \
	socket \
	strcasecmp \
	strchr \
	strerror \
	strncasecmp \
	strpbrk \
	strrchr \
	strstr \
	strtoul \
	strtoll \
],, [
    AC_MSG_ERROR([$ac_func is required; it should be part of libc.])
])

AC_REPLACE_FUNCS([ \
	strlcpy \
	strlcat \
])

AS_IF([test "x$host_os" = "x${host_os%mingw32}"], [
  # Check for missing features/flags
  AXEL_CHECK_MACRO([O_NONBLOCK], [fcntl.h])
])

# Optional (but included-by-default) ssl support
AC_ARG_WITH([ssl],
    AC_HELP_STRING([--without-ssl],[disable TLS support]), [], [with_ssl=yes])

AM_CONDITIONAL([WITH_SSL], [test "x$with_ssl" != xno])
AS_IF([test "x$with_ssl" != xno], [
    AS_IF([test "x$with_ssl" = xyes], [], [
        PKG_CONFIG_PATH="$with_ssl/lib/pkgconfig$PATH_SEPARATOR$PKG_CONFIG_PATH"
        export PKG_CONFIG_PATH
    ])
    PKG_CHECK_MODULES([SSL], [openssl])
    AC_DEFINE([HAVE_SSL], [1], [SSL])
    save_LIBS="$LIBS"
    LIBS="$LIBS $SSL_LIBS"
    AC_REPLACE_FUNCS([ \
        ASN1_STRING_get0_data \
    ])
    LIBS="$save_LIBS"
], AC_MSG_NOTICE([SSL support disabled]))

# Add Gettext
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.11.1])

# POSIX threads
AX_PTHREAD()

#### Definitions ####

# BSD vs GNU date
AS_IF([date -j >/dev/null 2>&1], [
  pdate() { date -j -f '%Y-%m-%dT%H:%M:%SZ' "$@"; }
], [
  pdate() { date -d "$@"; }
])
RELDATE_PRETTY=$(pdate '_AXEL_RELDATE' '+%B %e, %Y')
AC_SUBST([RELDATE_PRETTY])

# etc path
AC_DEFINE_UNQUOTED([ETCDIR], ["/etc"], ["Define /etc directory path"])

#####################

AC_CONFIG_FILES([
	Makefile
	po/Makefile.in
])
AC_OUTPUT
