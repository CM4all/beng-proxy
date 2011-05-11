/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_INSTANCE_H
#define BENG_PROXY_LB_INSTANCE_H

#include "config.h"

#include <inline/list.h>

#include <event.h>

struct lb_instance {
    struct pool *pool;

    struct config cmdline;

    struct lb_config *config;

    struct event_base *event_base;

    struct list_head listeners;
    struct list_head connections;
    unsigned num_connections;

    bool should_exit;
    struct event sigterm_event, sigint_event, sigquit_event;
    struct event sighup_event;

    /* stock */
    struct balancer *balancer;
    struct hstock *tcp_stock;
    struct tcp_balancer *tcp_balancer;

    struct stock *pipe_stock;
};

struct client_connection;

void
init_signals(struct lb_instance *instance);

void
deinit_signals(struct lb_instance *instance);

#endif
