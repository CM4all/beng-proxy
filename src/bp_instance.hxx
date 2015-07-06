/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_INSTANCE_HXX
#define BENG_PROXY_INSTANCE_HXX

#include "config.hxx"
#include "shutdown_listener.h"
#include "bp_listener.hxx"
#include "event/DelayedTrigger.hxx"

#include <inline/list.h>

#include <forward_list>

#include <event.h>

struct Stock;
struct StockMap;
struct ControlServer;
struct LocalControl;

struct instance {
    struct pool *pool;

    struct config config;

    struct event_base *event_base;

    uint64_t http_request_counter;

    std::forward_list<BPListener> listeners;

    struct list_head connections;
    unsigned num_connections;

    bool should_exit;
    struct shutdown_listener shutdown_listener;
    struct event sighup_event;

    /* child management */
    DelayedTrigger respawn_trigger;
    struct list_head workers;
    unsigned num_workers;

    /**
     * The configured control channel server (see --control-listen),
     * nullptr if none was configured.
     */
    ControlServer *control_server;

    /**
     * The implicit per-process control server.  It listens on a local
     * socket "@beng-proxy:PID" and will accept connections only from
     * root or the beng-proxy user.
     */
    LocalControl *local_control_server;

    /* stock */
    struct tstock *translate_stock;
    struct tcache *translate_cache;
    struct balancer *balancer;
    StockMap *tcp_stock;
    struct tcp_balancer *tcp_balancer;
    struct memcached_stock *memcached_stock;

    /* cache */
    struct http_cache *http_cache;

    struct filter_cache *filter_cache;

    struct lhttp_stock *lhttp_stock;
    struct fcgi_stock *fcgi_stock;

    StockMap *was_stock;

    StockMap *delegate_stock;

    struct nfs_stock *nfs_stock;
    struct nfs_cache *nfs_cache;

    Stock *pipe_stock;

    struct resource_loader *resource_loader;

    instance();

    void ForkCow(bool inherit);

    /**
     * Handler for #CONTROL_FADE_CHILDREN
     */
    void FadeChildren();

private:
    void RespawnWorkerCallback();
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
