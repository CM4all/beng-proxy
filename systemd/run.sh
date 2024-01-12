#!/bin/bash

DAEMON_USER=
DAEMON_GROUP=
ALLOW_USER=
ALLOW_GROUP=
SPAWN_USER=
LOGGER_USER=cm4all-logger
ACCESS_LOGGER=""
PORT=""
LISTEN=""
TRANSLATION_SOCKET=""
UA_CLASSES=
SESSION_SAVE_PATH=
OPTIONS=""

test -f /etc/default/cm4all-beng-proxy && source /etc/default/cm4all-beng-proxy

for i in DAEMON_USER DAEMON_GROUP ALLOW_USER ALLOW_GROUP SPAWN_USER ACCESS_LOGGER PORT LISTEN UA_CLASSES; do
    if eval test -n \"\${$i}\"; then
        echo "Variable $i in /etc/default/cm4all-beng-proxy has been removed, please configure /etc/cm4all/beng/proxy/beng-proxy.conf instead" >&2
        exit 1
    fi
done

if test -n "$TRANSLATION_SOCKET"; then
    OPTIONS="$OPTIONS --translation-socket $TRANSLATION_SOCKET"
fi

if test -n "$SESSION_SAVE_PATH"; then
    OPTIONS="$OPTIONS --set session_save_path=$SESSION_SAVE_PATH"
fi

if test -n "$VERBOSE_RESPONSE"; then
    OPTIONS="$OPTIONS --set verbose_response=$VERBOSE_RESPONSE"
fi

exec /usr/sbin/cm4all-beng-proxy \
    --logger-user "$LOGGER_USER" \
    $UASPEC \
    $OPTIONS
