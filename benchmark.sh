#!/bin/sh -e
rm -rf build/release
exec meson . build/release --buildtype=release -Dprefix=/usr/local/stow/cm4all-beng-proxy -Dwerror=true -Db_ndebug=true
