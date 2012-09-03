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

struct listener_node {
    struct list_head siblings;

    struct listener *listener;
};

struct instance {
    struct pool *pool;

    struct config config;

    struct event_base *event_base;

    uint64_t http_request_counter;

    struct list_head listeners;
    struct list_head connections;
    unsigned num_connections;

    bool should_exit;
    struct event sigterm_event, sigint_event, sigquit_event;
    struct event sighup_event;

    /* child management */
    struct event respawn_event;
    struct list_head workers;
    unsigned num_workers;

    struct control_server *control_server;

    /* stock */
    struct tcache *translate_cache;
    struct balancer *balancer;
    struct hstock *tcp_stock;
    struct tcp_balancer *tcp_balancer;
    struct memcached_stock *memcached_stock;

    /* cache */
    struct http_cache *http_cache;

    struct filter_cache *filter_cache;

    struct hstock *fcgi_stock;

    struct hstock *was_stock;

    struct hstock *delegate_stock;

    struct stock *pipe_stock;

    struct resource_loader *resource_loader;
};

struct client_connection;

void
init_signals(struct instance *instance);

void
deinit_signals(struct instance *instance);

void
all_listeners_event_add(struct instance *instance);

void
all_listeners_event_del(struct instance *instance);

#endif
