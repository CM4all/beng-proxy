#!/bin/sh
#
# Correct Content-Length response header.

echo -en "Content-Length: 4\n\n1234"
