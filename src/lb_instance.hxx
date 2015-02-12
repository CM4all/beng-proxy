/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_INSTANCE_H
#define BENG_PROXY_LB_INSTANCE_H

#include "lb_cmdline.hxx"
#include "shutdown_listener.h"
#include "lb_connection.hxx"
#include "lb_listener.hxx"

#include <inline/list.h>

#include <event.h>

#include <forward_list>

struct Stock;
struct StockMap;

struct lb_instance {
    struct pool *pool;

    struct lb_cmdline cmdline;

    struct lb_config *config;

    struct event_base *event_base;

    uint64_t http_request_counter = 0;

    struct list_head controls;

    std::forward_list<lb_listener> listeners;

    boost::intrusive::list<struct lb_connection,
                           boost::intrusive::constant_time_size<true>> connections;

    bool should_exit = false;
    struct shutdown_listener shutdown_listener;
    struct event sighup_event;

    /* stock */
    struct balancer *balancer;
    StockMap *tcp_stock;
    struct tcp_balancer *tcp_balancer;

    Stock *pipe_stock;

    unsigned FlushSSLSessionCache(long tm);

    lb_instance() {
        list_init(&controls);
    }
};

struct client_connection;

void
init_signals(struct lb_instance *instance);

void
deinit_signals(struct lb_instance *instance);

#endif
