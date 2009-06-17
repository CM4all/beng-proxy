#!/bin/sh

set -e

rm -rf config.cache build
mkdir build
aclocal
automake --add-missing --foreign
autoconf
CFLAGS="-O0 -ggdb" ./configure \
	--prefix=/usr/local/stow/cm4all-beng-proxy \
	--enable-debug
