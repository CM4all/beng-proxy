#!/bin/bash
#
# A CGI filter.
#
# Author: Max Kellermann <mk@cm4all.com>

echo "Content-Type: text/plain; charset=UTF-8"
echo
sed -e 's,Hello,Good morning,'
