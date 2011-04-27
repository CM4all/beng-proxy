#!/bin/sh
#
# Incorrect Content-Length response header: too small.

echo -en "Content-Length: 3\n\n1234"
