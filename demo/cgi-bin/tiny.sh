#!/bin/bash
#
# Generate a tiny response that will generate only one read() in
# beng-proxy for headers and body combined.

echo -en "Content-Type: text/plain\n\nHello world"
