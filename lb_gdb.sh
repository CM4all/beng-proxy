#!/bin/sh
set -e
ninja -C output/asan cm4all-beng-lb
exec gdb -x gdbrun --args ./output/asan/cm4all-beng-lb -vvvvv "$@"
