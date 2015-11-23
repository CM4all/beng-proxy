/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_INSTANCE_H
#define BENG_PROXY_LB_INSTANCE_H

#include "lb_cmdline.hxx"
#include "lb_connection.hxx"
#include "lb_listener.hxx"
#include "lb_control.hxx"
#include "event/Base.hxx"
#include "event/TimerEvent.hxx"
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"

#include <assert.h>

#include <forward_list>

struct Stock;
struct StockMap;
struct TcpBalancer;

struct lb_instance {
    struct pool *pool;

    struct lb_cmdline cmdline;

    struct lb_config *config;

    EventBase event_base;

    uint64_t http_request_counter = 0;

    std::forward_list<LbControl> controls;

    std::forward_list<lb_listener> listeners;

    TimerEvent launch_worker_event;

    boost::intrusive::list<struct lb_connection,
                           boost::intrusive::constant_time_size<true>> connections;

    /**
     * Number of #lb_tcp instances.
     */
    unsigned n_tcp_connections = 0;

    bool should_exit = false;
    ShutdownListener shutdown_listener;
    SignalEvent sighup_event;

    /* stock */
    struct balancer *balancer;
    StockMap *tcp_stock;
    TcpBalancer *tcp_balancer;

    Stock *pipe_stock;

    lb_instance()
        :shutdown_listener(ShutdownCallback, this) {}

    ~lb_instance() {
        assert(n_tcp_connections == 0);
    }

    unsigned FlushSSLSessionCache(long tm);

    static void ShutdownCallback(void *ctx);
};

struct client_connection;

void
init_signals(struct lb_instance *instance);

void
deinit_signals(struct lb_instance *instance);

#endif
