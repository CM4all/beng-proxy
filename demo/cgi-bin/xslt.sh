#!/bin/bash
#
# A trivial beng-proxy CGI filter which applies XSLT to an XML input using xsltproc.
#
# Author: Max Kellermann <mk@cm4all.com>

echo "Content-Type: text/html; charset=utf-8"
echo
exec /usr/bin/xsltproc /usr/share/cm4all/beng-proxy/demo/filter.xsl -
