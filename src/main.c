/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tpool.h"
#include "direct.h"
#include "instance.h"
#include "worker.h"
#include "connection.h"
#include "session.h"
#include "tstock.h"
#include "tcp-stock.h"
#include "memcached-stock.h"
#include "stock.h"
#include "tcache.h"
#include "http-cache.h"
#include "fcgi-stock.h"
#include "delegate-stock.h"
#include "fcache.h"
#include "child.h"
#include "global.h"
#include "failure.h"
#include "bulldog.h"
#include "balancer.h"
#include "listener.h"
#include "pipe-stock.h"

#include <daemon/daemonize.h>

#include <assert.h>
#include <unistd.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>

#include <event.h>

#ifndef NDEBUG
bool debug_mode = false;
#endif

static void
free_all_listeners(struct instance *instance)
{
    for (struct listener_node *node = (struct listener_node *)instance->listeners.next;
         &node->siblings != &instance->listeners;
         node = (struct listener_node *)node->siblings.next)
        listener_free(&node->listener);
    list_init(&instance->listeners);
}

void
all_listeners_event_add(struct instance *instance)
{
    for (struct listener_node *node = (struct listener_node *)instance->listeners.next;
         &node->siblings != &instance->listeners;
         node = (struct listener_node *)node->siblings.next)
        listener_event_add(node->listener);
}

void
all_listeners_event_del(struct instance *instance)
{
    for (struct listener_node *node = (struct listener_node *)instance->listeners.next;
         &node->siblings != &instance->listeners;
         node = (struct listener_node *)node->siblings.next)
        listener_event_del(node->listener);
}

static void
exit_event_callback(int fd __attr_unused, short event __attr_unused, void *ctx)
{
    struct instance *instance = (struct instance*)ctx;

    if (instance->should_exit)
        return;

    instance->should_exit = true;
    deinit_signals(instance);

    free_all_listeners(instance);

    while (!list_empty(&instance->connections))
        close_connection((struct client_connection*)instance->connections.next);

    pool_commit();

    children_shutdown();
    worker_killall(instance);

    session_manager_deinit();

    if (instance->translate_cache != NULL)
        translate_cache_close(instance->translate_cache);

    if (instance->http_cache != NULL) {
        http_cache_close(instance->http_cache);
        instance->http_cache = NULL;
    }

    if (instance->filter_cache != NULL) {
        filter_cache_close(instance->filter_cache);
        instance->filter_cache = NULL;
    }

    if (instance->fcgi_stock != NULL) {
        fcgi_stock_kill(instance->fcgi_stock);
        instance->fcgi_stock = NULL;
    }

    if (instance->memcached_stock != NULL)
        memcached_stock_free(instance->memcached_stock);

    if (instance->tcp_stock != NULL)
        hstock_free(instance->tcp_stock);

    if (instance->balancer != NULL)
        balancer_free(instance->balancer);

    if (instance->delegate_stock != NULL)
        hstock_free(instance->delegate_stock);

    if (instance->pipe_stock != NULL)
        stock_free(instance->pipe_stock);

    pool_commit();
}

static void
reload_event_callback(int fd __attr_unused, short event __attr_unused,
                      void *ctx)
{
    struct instance *instance = (struct instance*)ctx;

    daemonize_reopen_logfile();

    translate_cache_flush(instance->translate_cache);
    http_cache_flush(instance->http_cache);
    filter_cache_flush(instance->filter_cache);
}

void
init_signals(struct instance *instance)
{
    signal(SIGPIPE, SIG_IGN);

    event_set(&instance->sigterm_event, SIGTERM, EV_SIGNAL|EV_PERSIST,
              exit_event_callback, instance);
    event_add(&instance->sigterm_event, NULL);

    event_set(&instance->sigint_event, SIGINT, EV_SIGNAL|EV_PERSIST,
              exit_event_callback, instance);
    event_add(&instance->sigint_event, NULL);

    event_set(&instance->sigquit_event, SIGQUIT, EV_SIGNAL|EV_PERSIST,
              exit_event_callback, instance);
    event_add(&instance->sigquit_event, NULL);

    event_set(&instance->sighup_event, SIGHUP, EV_SIGNAL|EV_PERSIST,
              reload_event_callback, instance);
    event_add(&instance->sighup_event, NULL);
}

void
deinit_signals(struct instance *instance)
{
    event_del(&instance->sigterm_event);
    event_del(&instance->sigint_event);
    event_del(&instance->sigquit_event);
    event_del(&instance->sighup_event);
}

static void
add_listener(struct instance *instance, struct addrinfo *ai)
{
    assert(ai != NULL);

    do {
        struct listener_node *node = p_malloc(instance->pool, sizeof(*node));

        node->listener = listener_new(instance->pool, ai->ai_family, ai->ai_socktype,
                                      ai->ai_protocol, ai->ai_addr,
                                      ai->ai_addrlen,
                                      &http_listener_callback, instance);
        if (node->listener == NULL) {
            perror("listener_tcp_port_new() failed");
            exit(2);
        }

        list_add(&node->siblings, &instance->listeners);

        ai = ai->ai_next;
    } while (ai != NULL);
}

static void
add_tcp_listener(struct instance *instance, int port)
{
    struct listener_node *node = p_malloc(instance->pool, sizeof(*node));
    int ret;

    ret = listener_tcp_port_new(instance->pool, port,
                                &http_listener_callback, instance,
                                &node->listener);
    if (ret < 0) {
        perror("listener_tcp_port_new() failed");
        exit(2);
    }

    list_add(&node->siblings, &instance->listeners);
}

int main(int argc, char **argv)
{
    int ret;
    bool bret;
    int __attr_unused ref;
    static struct instance instance = {
        .config = {
            .max_connections = 8192,
            .http_cache_size = 512 * 1024 * 1024,
            .filter_cache_size = 128 * 1024 * 1024,
            .translate_cache_size = 131072,
        },
    };

#ifndef NDEBUG
    if (geteuid() != 0)
        debug_mode = true;
#endif

    direct_global_init();

    instance.pool = pool_new_libc(NULL, "global");
    tpool_init(instance.pool);

    /* configuration */

    instance.config.document_root = "/var/www";

    parse_cmdline(&instance.config, instance.pool, argc, argv);

    if (instance.config.num_ports == 0 && instance.config.num_listen == 0) {
        instance.config.ports[instance.config.num_ports++] =
            debug_mode ? 8080 : 80;
    }

    /* initialize */

    instance.event_base = event_init();

    list_init(&instance.listeners);
    list_init(&instance.connections);
    list_init(&instance.workers);

    init_signals(&instance);

    children_init(instance.pool);

    bret = session_manager_init();
    if (!bret) {
        fprintf(stderr, "session_manager_init() failed\n");
        exit(2);
    }

    for (unsigned i = 0; i < instance.config.num_ports; ++i)
        add_tcp_listener(&instance, instance.config.ports[i]);

    for (unsigned i = 0; i < instance.config.num_listen; ++i)
        add_listener(&instance, instance.config.listen[i]);

    instance.balancer = balancer_new(instance.pool);
    instance.tcp_stock = tcp_stock_new(instance.pool, instance.balancer, 256);

    if (instance.config.memcached_server != NULL)
        instance.memcached_stock =
            memcached_stock_new(instance.pool, instance.tcp_stock,
                                instance.config.memcached_server);

    if (instance.config.translation_socket != NULL) {
        struct tstock *translate_stock =
            tstock_new(instance.pool, instance.tcp_stock,
                       instance.config.translation_socket);

        instance.translate_cache = translate_cache_new(instance.pool, translate_stock,
                                                       instance.config.translate_cache_size);
    }

    instance.http_cache = http_cache_new(instance.pool,
                                         instance.config.http_cache_size,
                                         instance.memcached_stock,
                                         instance.tcp_stock);
    instance.fcgi_stock = fcgi_stock_new(instance.pool);
    instance.delegate_stock = delegate_stock_new(instance.pool);
    instance.pipe_stock = pipe_stock_new(instance.pool);
    instance.filter_cache = filter_cache_new(instance.pool,
                                             instance.config.filter_cache_size,
                                             instance.tcp_stock,
                                             instance.fcgi_stock);

    failure_init(instance.pool);
    bulldog_init(instance.config.bulldog_path);

    global_translate_cache = instance.translate_cache;
    global_tcp_stock = instance.tcp_stock;
    global_memcached_stock = instance.memcached_stock;
    global_http_cache = instance.http_cache;
    global_fcgi_stock = instance.fcgi_stock;
    global_delegate_stock = instance.delegate_stock;
    global_filter_cache = instance.filter_cache;
    global_pipe_stock = instance.pipe_stock;

    /* daemonize */

#ifndef PROFILE
    ret = daemonize();
    if (ret < 0)
        exit(2);
#endif

    /* create worker processes */

    if (instance.config.num_workers > 0) {
        pid_t pid;

        /* the master process shouldn't work */
        for (struct listener_node *node = (struct listener_node *)instance.listeners.next;
             &node->siblings != &instance.listeners;
             node = (struct listener_node *)node->siblings.next)
            listener_event_del(node->listener);

        while (instance.num_workers < instance.config.num_workers) {
            pid = worker_new(&instance);
            if (pid <= 0)
                break;
        }
    }

    /* main loop */

    event_dispatch();

    /* cleanup */

    bulldog_deinit();
    failure_deinit();

    free_all_listeners(&instance);

#ifndef PROFILE
    event_base_free(instance.event_base);
#endif

    tpool_deinit();
    ref = pool_unref(instance.pool);
    assert(ref == 0);
    pool_commit();

    pool_recycler_clear();

    daemonize_cleanup();

    direct_global_deinit();
}
