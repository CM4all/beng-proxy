AC_PREREQ(2.60)
AC_INIT(CM4all BENG-Proxy, 0.7.1, mk@cm4all.com, cm4all-beng-proxy)
AC_COPYRIGHT(Copyright 2007-2009 Content Management AG)
AC_CONFIG_SRCDIR(src/main.c)
AC_CONFIG_AUX_DIR(build)
AM_INIT_AUTOMAKE([foreign 1.10 no-dist subdir-objects])

dnl Checks for programs.

AC_PROG_MAKE_SET
AC_PROG_INSTALL
AC_PROG_CC_C99
AC_PROG_RANLIB

dnl Checks for OS features

AC_SYS_LARGEFILE

dnl Checks for libraries.

PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.16],,
	[AC_MSG_ERROR([glib 2.16 is required])])

PKG_CHECK_MODULES([LIBCM4ALL_DAEMON], [libcm4all-daemon >= 0.1.2],,
	[AC_MSG_ERROR([libcm4all-daemon not found])])

PKG_CHECK_MODULES([LIBCM4ALL_INLINE], [libcm4all-inline >= 0.1.1],,
	[AC_MSG_ERROR([libcm4all-inline not found])])

PKG_CHECK_MODULES([LIBCM4ALL_SOCKET], [libcm4all-socket],,
	[AC_MSG_ERROR([libcm4all-socket not found])])

dnl
dnl CFLAGS
dnl

AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_CPPFLAGS)

AM_CFLAGS="-W -Wall"
AM_CPPFLAGS="-D_REENTRANT"

if test "$am_cv_CC_dependencies_compiler_type" = "gcc3"; then
	AM_CFLAGS="$AM_CFLAGS -Wall -Wextra -Wmissing-prototypes -Wwrite-strings -Wcast-qual -Wfloat-equal -Wshadow -Wpointer-arith -Wbad-function-cast -Wsign-compare -Waggregate-return -Wmissing-declarations -Wmissing-noreturn -Wmissing-format-attribute -Wredundant-decls -Wnested-externs -Wno-long-long -Wstrict-prototypes -Wundef -pedantic"
fi

dnl
dnl Compile-time options
dnl

AC_ARG_ENABLE(documentation,
	AS_HELP_STRING([--enable-documentation],
		[Enables documentation (default: disabled)]),,
	enable_documentation=no)
AM_CONDITIONAL(DOCUMENTATION, test x$enable_documentation = xyes)

AC_ARG_ENABLE(deflate,
	AS_HELP_STRING([--enable-deflate],
		[Enables deflate support (default: disabled)]),,
	enable_deflate=no)
AM_CONDITIONAL(DEFLATE, test x$enable_deflate = xyes)

dnl
dnl DEBUG
dnl

AC_ARG_ENABLE(debug,
	AS_HELP_STRING([--disable-debug],
		[Disable debugging (default: enabled)]),,
	enable_debug=yes)
AM_CONDITIONAL(TEST, test x$enable_test = xyes)

if test x$enable_debug = xyes; then
	AM_CFLAGS="$AM_CFLAGS -Werror -pedantic-errors"
	AM_CPPFLAGS="$AM_CPPFLAGS -DPOISON -DVALGRIND"
	AM_CPPFLAGS="$AM_CPPFLAGS -DTRACE"
	AM_CPPFLAGS="$AM_CPPFLAGS -DDEBUG_POOL_REF"
	AM_CPPFLAGS="$AM_CPPFLAGS -DDEBUG_POOL_GROW -DDUMP_POOL_SIZE -DCACHE_LOG"
	# -DISTREAM_POOL
	# -DDUMP_POOL_UNREF
	# -DDUMP_WIDGET_TREE
	# -DDUMP_POOL_ALLOC_ALL
else
	AM_CPPFLAGS="$AM_CPPFLAGS -DNDEBUG"
fi

AC_ARG_ENABLE(gprof,
	AS_HELP_STRING([--enable-gprof],
		[enable profiling via gprof (default: disabled)]),,
	enable_gprof=no)
AM_CONDITIONAL(GPROF, test x$enable_gprof = xyes)

AC_ARG_ENABLE(unit-test,
	AS_HELP_STRING([--enable-unit-test],
		[Enable the unit test suite]),,
	enable_test=$enable_debug)
AM_CONDITIONAL(TEST, test x$enable_test = xyes)

dnl
dnl Output files
dnl

AC_OUTPUT(
Makefile
libcm4all-beng-proxy.pc
libcm4all-istream.pc
)
