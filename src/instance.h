/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_INSTANCE_H
#define __BENG_INSTANCE_H

#include "config.h"

#include <inline/list.h>

#include <event.h>

struct worker {
    struct list_head siblings;

    struct instance *instance;

    pid_t pid;
};

struct instance {
    pool_t pool;

    struct config config;

    struct event_base *event_base;

    struct listener *listener;
    struct list_head connections;
    unsigned num_connections;

    bool should_exit;
    struct event sigterm_event, sigint_event, sigquit_event;
    struct event sighup_event;

    /* child management */
    struct event respawn_event;
    struct list_head workers;
    unsigned num_workers;

    /* stock */
    struct tcache *translate_cache;
    struct hstock *tcp_stock;
    struct hstock *ajp_client_stock;

    /* cache */
    struct http_cache *http_cache;
};

struct client_connection;

void
init_signals(struct instance *instance);

void
deinit_signals(struct instance *instance);

pid_t
worker_new(struct instance *instance);

void
worker_killall(struct instance *instance);

#endif
