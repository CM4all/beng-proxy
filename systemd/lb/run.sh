#!/bin/bash

RUNDIR=/var/run/cm4all
DAEMON_USER=cm4all-beng-lb
LOGGER=""
LOGGER_USER=cm4all-logger
ACCESS_LOGGER=""
CONFIG_FILE="/etc/cm4all/beng/lb.conf"
OPTIONS="-vv"

test -f /etc/default/cm4all-beng-lb && source /etc/default/cm4all-beng-lb

install -d -m 0711 $RUNDIR
exec /usr/sbin/cm4all-beng-lb \
    --no-daemon \
    --user "$DAEMON_USER" \
    --config-file "$CONFIG_FILE" \
    --logger "$LOGGER" \
    --logger-user "$LOGGER_USER" \
    --access-logger "$ACCESS_LOGGER" \
    --watchdog \
    $OPTIONS
