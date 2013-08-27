#!/bin/bash

{
    echo "HTTP/1.1 100 Continue"
    echo
    echo "HTTP/1.1 100 Continue"
    echo
    echo "HTTP/1.1 200 OK"
    echo
} >&0

while read; do :; done
