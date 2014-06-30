/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_INSTANCE_H
#define BENG_PROXY_LB_INSTANCE_H

#include "config.hxx"
#include "shutdown_listener.h"
#include "lb_connection.hxx"

#include <inline/list.h>

#include <event.h>

struct lb_instance {
    struct pool *pool;

    struct config cmdline;

    struct lb_config *config;

    struct event_base *event_base;

    uint64_t http_request_counter;

    struct list_head controls;
    struct list_head listeners;

    boost::intrusive::list<struct lb_connection,
                           boost::intrusive::constant_time_size<true>> connections;

    bool should_exit;
    struct shutdown_listener shutdown_listener;
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
