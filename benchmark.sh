#!/bin/sh

set -e

./configure \
	CC=clang CXX=clang++ \
	CFLAGS="-O2 -ggdb" CXXFLAGS="-O2 -ggdb -D__STRICT_ANSI__" \
	--prefix=/usr/local/stow/cm4all-beng-proxy \
	--disable-debug \
	--enable-werror \
	--enable-silent-rules \
	"$@"
