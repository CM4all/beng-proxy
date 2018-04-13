#!/bin/sh
set -e
ninja -C output/asan cm4all-beng-proxy
exec gdb -x gdbrun --args ./output/asan/cm4all-beng-proxy -vvvvv "$@"
