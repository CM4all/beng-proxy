#!/bin/sh

set -e

rm -rf config.cache build
mkdir build
aclocal -I m4 $ACLOCAL_FLAGS
automake --add-missing --foreign
autoconf

DEBUG_FLAGS="-O0 -ggdb -fsanitize=address"

unset CLANG_CXXFLAGS
if gcc -dumpversion 2>/dev/null |grep -q '^[0-4]'; then
    # Use clang if gcc is older than version 5.
    export CC=clang
    export CXX=clang++
    CLANG_CXXFLAGS="-D__STRICT_ANSI__"
fi

./configure \
	CFLAGS="$DEBUG_FLAGS" CXXFLAGS="$DEBUG_FLAGS $CLANG_CXXFLAGS" \
	--prefix=/usr/local/stow/cm4all-beng-proxy \
	--enable-debug \
	--enable-silent-rules \
	"$@"
