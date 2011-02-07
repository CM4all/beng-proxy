#!/bin/bash
#
# A trivial beng-proxy CGI filter which outputs the input file as-is.
#
# Author: Max Kellermann <mk@cm4all.com>

echo "Content-Type: $CONTENT_TYPE"
echo
exec cat
