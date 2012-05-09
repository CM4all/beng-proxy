#!/bin/bash

if test "${REQUEST_URI: -4}" = "/foo"; then
    :
elif test "${REQUEST_URI: -5}" = "/path"; then
    echo "Status: 303"
    echo "Location: ${REQUEST_URI}/foo"
elif test "${REQUEST_URI: -9}" = "/absolute"; then
    echo "Status: 303"
    echo "Location: http://cfatest01.intern.cm-ag${REQUEST_URI}/foo"
elif test "${REQUEST_URI: -8}" = "/invalid"; then
    echo "Status: 303"
    echo "Location: http://example.com/"
else
    echo "Status: 303"
    echo "Location: foo"
fi

echo "Content-Type: text/plain"
echo ""
echo $REQUEST_URI
