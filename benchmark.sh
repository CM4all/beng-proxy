#!/bin/sh -e
rm -rf build/release
exec meson . build/release --buildtype=debugoptimized -Dprefix=/usr/local/stow/cm4all-beng-proxy --werror -Db_ndebug=true "$@"
