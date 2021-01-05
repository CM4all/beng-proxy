#!/bin/bash

# work around the boost::locale::facet::_S_create_c_locale exception
export LC_ALL=C

DAEMON_USER=
LOGGER_USER=cm4all-logger
ACCESS_LOGGER=""
CONFIG_FILE=
OPTIONS=""

test -f /etc/default/cm4all-beng-lb && source /etc/default/cm4all-beng-lb

for i in DAEMON_USER ACCESS_LOGGER CONFIG_FILE; do
    if eval test -n \"\${$i}\"; then
        echo "Variable $i in /etc/default/cm4all-beng-lb has been removed, please configure /etc/cm4all/beng/lb.conf instead" >&2
        exit 1
    fi
done

exec /usr/sbin/cm4all-beng-lb \
    --logger-user "$LOGGER_USER" \
    $OPTIONS
