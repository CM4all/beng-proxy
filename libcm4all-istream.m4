AC_DEFUN([AM_LIBCM4ALL_ISTREAM], [
        ok=no

        PKG_CHECK_MODULES(LIBCM4ALL_ISTREAM, libcm4all-istream, ok=yes,)

        if test x$ok = xno; then
                AC_MSG_ERROR(libcm4all-istream not found)
        fi
])
