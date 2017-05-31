#!/bin/sh
set -e
ninja -C build cm4all-beng-lb
exec gdb -x gdbrun --args ./build/cm4all-beng-lb -vvvvv
