#!/bin/bash
#
# Generate a large response header.

echo -n "X-Foo: "
for i in `seq 2048`; do
    echo -n $i
done
echo
