#!/bin/sh -e

ninja -C output/debug cm4all-beng-proxy

export G_SLICE=always-malloc

VALGRIND_FLAGS="--leak-check=yes --show-reachable=yes"
VALGRIND_FLAGS="$VALGRIND_FLAGS --track-fds=yes --track-origins=yes"
VALGRIND_FLAGS="$VALGRIND_FLAGS --suppressions=valgrind.suppressions"

exec valgrind $VALGRIND_FLAGS ./output/debug/cm4all-beng-proxy -vvvvv "$@"
