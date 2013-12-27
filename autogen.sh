#!/bin/sh

set -e

rm -rf config.cache build
mkdir build
aclocal
automake --add-missing --foreign
autoconf
./configure \
	CC=clang CXX=clang++ \
	CFLAGS="-O0 -ggdb" CXXFLAGS="-O0 -ggdb -D__STRICT_ANSI__" \
	--prefix=/usr/local/stow/cm4all-beng-proxy \
	--enable-debug \
	--enable-silent-rules \
	"$@"
