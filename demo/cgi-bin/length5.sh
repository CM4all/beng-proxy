#!/bin/bash
#
# Correct Content-Length response header, large response body.

echo -en "Content-Length: 8192\n\n"
dd if=/dev/zero bs=1024 count=8 2>/dev/null
