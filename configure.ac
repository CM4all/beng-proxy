AC_PREREQ(2.60)
AC_INIT(CM4all BENG-Proxy, 2.0.33, mk@cm4all.com, cm4all-beng-proxy)
AC_COPYRIGHT(Copyright 2007-2012 Content Management AG)
AC_CONFIG_SRCDIR(src/main.c)
AC_CONFIG_AUX_DIR(build)
AM_INIT_AUTOMAKE([foreign 1.10 dist-bzip2 subdir-objects])

dnl Checks for programs.

AC_PROG_MAKE_SET
AC_PROG_INSTALL
AC_PROG_CC_C99
AC_PROG_RANLIB

dnl Checks for LLVM and clang

AC_ARG_ENABLE(llvm,
	AS_HELP_STRING([--enable-llvm], [Enable LLVM bitcode library]),,
	[enable_llvm=no])
AM_CONDITIONAL(LLVM, test x$enable_llvm = xyes)

AC_ARG_WITH([clang],
	AS_HELP_STRING([--with-clang=PATH],
		 [Location of the clang program]),
	[], [with_clang=no])

AC_ARG_WITH([llvm-config],
	AS_HELP_STRING([--with-llvm-config=PATH],
		 [Location of the llvm-config program]))

if test x$enable_llvm = xyes; then
	if test x$with_clang = xno; then
		AC_PATH_PROG([CLANG], [clang], AC_MSG_ERROR([clang not found]))
	else
		CLANG=$with_clang
	fi

	if test x$with_llvm_config = x; then
		AC_PATH_PROGS([with_llvm_config],
			[llvm-config llvm-config-3.1 llvm-config-3.0],
			AC_MSG_ERROR([llvm-config not found]))
	fi

	llvm_bin=`$with_llvm_config --bindir`

	AC_PATH_PROG([LLVM_LINK], [llvm-link],
		AC_MSG_ERROR([llvm-link not found]), [$llvm_bin])
	AC_PATH_PROG([LLVM_OPT], [opt],
		AC_MSG_ERROR([opt not found]), [$llvm_bin])
	AC_PATH_PROG([LLVM_LLC], [llc],
		AC_MSG_ERROR([llc not found]), [$llvm_bin])
fi

dnl Checks for OS features

AC_SYS_LARGEFILE

AC_CHECK_FUNCS(pipe2 accept4)

AC_CHECK_HEADERS(attr/xattr.h)
AM_CONDITIONAL(HAVE_ATTR_XATTR_H, test x$ac_cv_header_attr_xattr_h = xyes)

dnl Checks for libraries.

PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.16],,
	[AC_MSG_ERROR([glib 2.16 is required])])

PKG_CHECK_MODULES([LIBSSL], [openssl],,
	[AC_MSG_ERROR([OpenSSL not found])])

PKG_CHECK_MODULES([LIBCM4ALL_DAEMON], [libcm4all-daemon >= 0.1.2],,
	[AC_MSG_ERROR([libcm4all-daemon not found])])

PKG_CHECK_MODULES([LIBCM4ALL_INLINE], [libcm4all-inline >= 0.1.1],,
	[AC_MSG_ERROR([libcm4all-inline not found])])

PKG_CHECK_MODULES([LIBCM4ALL_HTTP], [libcm4all-http],,
	[AC_MSG_ERROR([libcm4all-http not found])])

PKG_CHECK_MODULES([LIBCM4ALL_SOCKET], [libcm4all-socket >= 0.3.1],,
	[AC_MSG_ERROR([libcm4all-socket not found])])

PKG_CHECK_MODULES([LIBWAS], [libcm4all-was-simple],,
	[AC_MSG_ERROR([libwas not found])])

AC_CHECK_HEADERS(valgrind/memcheck.h)

dnl
dnl CFLAGS
dnl

AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_CPPFLAGS)
AC_SUBST(AM_LDFLAGS)

AM_CFLAGS="-W -Wall"
AM_CPPFLAGS="-D_REENTRANT"
AM_LDFLAGS=""

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

AC_ARG_WITH(session-id,
	AS_HELP_STRING([--session-id=SIZE],
		[Sets the size of the session id (default: 8)]))
if test x$with_session_id != x; then
	AC_DEFINE_UNQUOTED([SESSION_ID_SIZE], $with_session_id)
fi

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
	AM_CPPFLAGS="$AM_CPPFLAGS -DPOISON"
	AM_CPPFLAGS="$AM_CPPFLAGS -DTRACE"
	AM_CPPFLAGS="$AM_CPPFLAGS -DDEBUG_POOL_REF"
	AM_CPPFLAGS="$AM_CPPFLAGS -DCACHE_LOG"
	# -DISTREAM_POOL
	# -DDUMP_POOL_UNREF
	# -DDUMP_WIDGET_TREE
	# -DDUMP_POOL_ALLOC_ALL

	if test x$ac_cv_header_valgrind_memcheck_h = xyes; then
		AM_CPPFLAGS="$AM_CPPFLAGS -DVALGRIND"
	fi
else
	AM_CFLAGS="$AM_CFLAGS -ffunction-sections -fdata-sections"
	AM_CPPFLAGS="$AM_CPPFLAGS -DNDEBUG"
	AM_LDFLAGS="$AM_LDFLAGS -Wl,-gc-sections"
fi

AC_ARG_ENABLE(dump-pool-size,
	AS_HELP_STRING([--enable-dump-pool-size],
		[Log the final size of pools (default: disabled)]),,
	enable_dump_pool_size=no)
if test x$enable_dump_pool_size = xyes; then
	AC_DEFINE(DUMP_POOL_SIZE, 1)
fi

AC_ARG_ENABLE(dump-pool-grow,
	AS_HELP_STRING([--enable-dump-pool-grow],
		[Log when a pool grows (default: disabled)]),,
	enable_dump_pool_grow=no)
if test x$enable_dump_pool_grow = xyes; then
	AC_DEFINE(DEBUG_POOL_GROW, 1)
fi

AC_ARG_ENABLE(stopwatch,
	AS_HELP_STRING([--enable-stopwatch],
		[Measure the time it takes to load resources]),,
	enable_stopwatch=$enable_debug)
AM_CONDITIONAL(ENABLE_STOPWATCH, test x$enable_stopwatch = xyes)
if test x$enable_stopwatch = xyes; then
	AC_DEFINE(ENABLE_STOPWATCH, 1)
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
