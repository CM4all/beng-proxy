/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_INSTANCE_H
#define __BENG_INSTANCE_H

#include "listener.h"
#include "list.h"
#include "config.h"

#include <event.h>

struct child {
    struct list_head siblings;
    pid_t pid;
};

struct instance {
    pool_t pool;

    struct config config;

    struct event_base *event_base;

    listener_t listener;
    struct list_head connections;
    unsigned num_connections;

    int should_exit;
    struct event sigterm_event, sigint_event, sigquit_event;

    /* child management */
    struct event child_event;
    struct list_head children;
    unsigned num_children;
};

struct client_connection;

void
init_signals(struct instance *instance);

void
deinit_signals(struct instance *instance);

pid_t
create_child(struct instance *instance);

void
kill_children(struct instance *instance);

#endif
