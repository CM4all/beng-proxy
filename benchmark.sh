#!/bin/sh

set -e

CFLAGS="-O2 -ggdb" ./configure \
	--prefix=/usr/local/stow/cm4all-beng-proxy \
	--disable-debug \
	"$@"
