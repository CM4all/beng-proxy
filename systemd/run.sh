#!/bin/bash

# work around the boost::locale::facet::_S_create_c_locale exception
export LC_ALL=C

RUNDIR=/var/run/cm4all
DAEMON_USER=cm4all-beng-proxy
DAEMON_GROUP=
ALLOW_USER=
ALLOW_GROUP=
SPAWN_USER=www-data
LOGGER_USER=cm4all-logger
ACCESS_LOGGER="null"
PORT=""
LISTEN=""
WORKERS=0
DOCUMENT_ROOT=/var/www
TRANSLATION_SOCKET=""
UA_CLASSES=
SESSION_SAVE_PATH=
OPTIONS=""

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
    --user "$DAEMON_USER" \
    --allow-user "$ALLOW_USER" \
    --allow-group "$ALLOW_GROUP" \
    --spawn-user "$SPAWN_USER" \
    --logger-user "$LOGGER_USER" \
    --access-logger "$ACCESS_LOGGER" \
    $PORTSPEC $LISTENSPEC $UASPEC \
    --workers "$WORKERS" \
    --document-root "$DOCUMENT_ROOT" \
    $OPTIONS
