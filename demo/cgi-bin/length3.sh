#!/bin/sh
#
# Incorrect Content-Length response header: too large.

echo -en "Content-Length: 5\n\n1234"
