#!/bin/bash

{
    echo "HTTP/1.1 200 OK"
    echo "Content-Length: 3"
    echo
    echo -n "foo"
} >&0

while read; do :; done
