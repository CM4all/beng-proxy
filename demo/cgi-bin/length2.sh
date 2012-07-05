#!/bin/bash
#
# Incorrect Content-Length response header: too small.

/bin/echo -en "Content-Length: 3\n\n1234"
