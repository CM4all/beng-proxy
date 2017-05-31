#!/bin/sh
set -e
ninja -C build cm4all-beng-proxy
exec gdb -x gdbrun --args ./build/cm4all-beng-proxy -vvvvv "$@"
