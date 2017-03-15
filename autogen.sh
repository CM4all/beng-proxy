#!/bin/sh -e
rm -rf build
exec meson . build -Dprefix=/usr/local/stow/cm4all-beng-proxy --werror -Db_sanitize=address "$@"
