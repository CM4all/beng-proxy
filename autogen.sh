#!/bin/sh

set -e

rm -rf config.cache build
mkdir build
aclocal
automake --add-missing --foreign
autoconf
./configure \
	CFLAGS="-O0 -ggdb" CXXFLAGS="-O0 -ggdb" \
	--prefix=/usr/local/stow/cm4all-beng-proxy \
	--enable-debug \
	--enable-silent-rules \
	"$@"
