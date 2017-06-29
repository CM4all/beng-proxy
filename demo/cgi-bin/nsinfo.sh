#!/bin/bash

echo "Content-Type: text/plain"
echo
echo PID=$$
echo
id
echo
cat /proc/self/mountinfo
echo
ps faux
