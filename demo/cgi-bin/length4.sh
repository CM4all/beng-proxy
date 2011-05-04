#!/bin/bash
#
# Incorrect Content-Length response header: too small, late detection.

echo -en "Content-Length: 4\n\n123"
sleep 0.1
echo -n "45"
