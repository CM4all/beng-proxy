#!/bin/bash

RUNDIR=/var/run/cm4all
PIDFILE=$RUNDIR/beng-lb.pid
DAEMON_USER=cm4all-beng-lb
LOGGER="exec /usr/bin/multilog t s4194304 n16 /var/log/cm4all/beng-lb"
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
