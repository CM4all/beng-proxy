AC_DEFUN([AM_LIBCM4ALL_BENG_PROXY], [
        ok=no

        PKG_CHECK_MODULES(LIBCM4ALL_BENG_PROXY, libcm4all-beng-proxy, ok=yes,)

        if test x$ok = xno; then
                AC_MSG_ERROR(libcm4all-beng-proxy not found)
        fi
])
