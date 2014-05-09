#!/bin/bash

RUNDIR=/var/run/cm4all
PIDFILE=$RUNDIR/beng-proxy.pid
DAEMON_USER=cm4all-beng-proxy
DAEMON_GROUP=
LOGGER="exec /usr/bin/multilog t s4194304 n16 /var/log/cm4all/beng-proxy"
LOGGER_USER=cm4all-logger
ACCESS_LOGGER=""
PORT=80
WORKERS=1
DOCUMENT_ROOT=/var/www
TRANSLATION_SOCKET=$RUNDIR/beng-proxy-translation.sock
UA_CLASSES=
SESSION_SAVE_PATH=
OPTIONS="-vv"

test -f /etc/default/cm4all-beng-proxy && source /etc/default/cm4all-beng-proxy

test -n "$PORT" && PORTSPEC=`echo "$PORT" | xargs -rd, -n1 -- echo -n " --port"`
test -n "$LISTEN" && LISTENSPEC=`echo "$LISTEN" | xargs -rd, -n1 -- echo -n " --listen"`
test -n "$UA_CLASSES" && UASPEC="--ua-classes=${UA_CLASSES}"

if test -n "$DAEMON_GROUP"; then
    OPTIONS="$OPTIONS --group $DAEMON_GROUP"
fi

if test -n "$TRANSLATION_SOCKET"; then
    OPTIONS="$OPTIONS --translation-socket $TRANSLATION_SOCKET"
fi

if test -n "$SESSION_SAVE_PATH"; then
    OPTIONS="$OPTIONS --set session_save_path=$SESSION_SAVE_PATH"
fi

if test -n "$VERBOSE_RESPONSE"; then
    OPTIONS="$OPTIONS --set verbose_response=$VERBOSE_RESPONSE"
fi

install -d -m 0711 $RUNDIR
exec /usr/sbin/cm4all-beng-proxy \
    --no-daemon \
    --user "$DAEMON_USER" \
    --logger "$LOGGER" \
    --logger-user "$LOGGER_USER" \
    --access-logger "$ACCESS_LOGGER" \
    $PORTSPEC $LISTENSPEC $UASPEC \
    --workers "$WORKERS" \
    --document-root "$DOCUMENT_ROOT" \
    $OPTIONS
