#!/bin/bash

# work around the boost::locale::facet::_S_create_c_locale exception
export LC_ALL=C

RUNDIR=/var/run/cm4all
DAEMON_USER=cm4all-beng-lb
LOGGER_USER=cm4all-logger
ACCESS_LOGGER="null"
CONFIG_FILE="/etc/cm4all/beng/lb.conf"
OPTIONS=""

test -f /etc/default/cm4all-beng-lb && source /etc/default/cm4all-beng-lb

install -d -m 0711 $RUNDIR
exec /usr/sbin/cm4all-beng-lb \
    --user "$DAEMON_USER" \
    --config-file "$CONFIG_FILE" \
    --logger-user "$LOGGER_USER" \
    --access-logger "$ACCESS_LOGGER" \
    $OPTIONS
