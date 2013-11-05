/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tpool.h"
#include "direct.h"
#include "bp_instance.hxx"
#include "worker.h"
#include "connection.h"
#include "crash.h"
#include "fb_pool.h"
#include "session_manager.h"
#include "session_save.h"
#include "tstock.h"
#include "tcp-stock.h"
#include "tcp-balancer.h"
#include "memcached-stock.h"
#include "hstock.h"
#include "tcache.h"
#include "http_cache.h"
#include "lhttp_stock.h"
#include "fcgi-stock.h"
#include "was-stock.h"
#include "delegate-stock.h"
#include "fcache.h"
#include "child.h"
#include "thread_pool.h"
#include "global.h"
#include "failure.h"
#include "bulldog.h"
#include "balancer.h"
#include "listener.h"
#include "pipe-stock.h"
#include "resource-loader.h"
#include "bp_control.hxx"
#include "log-glue.h"
#include "ua_classification.h"
#include "ssl_init.hxx"
#include "ssl_client.h"
#include "capabilities.h"

#include <daemon/daemonize.h>
#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>

#include <event.h>

#ifdef HAVE_LIBNFS
#include "nfs_stock.h"
#include "nfs_cache.h"
#endif

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
shutdown_callback(void *ctx)
{
    struct instance *instance = (struct instance*)ctx;

    if (instance->should_exit)
        return;

    instance->should_exit = true;
    deinit_signals(instance);
    thread_pool_stop();

    free_all_listeners(instance);

    while (!list_empty(&instance->connections))
        close_connection((struct client_connection*)instance->connections.next);

    pool_commit();

    children_shutdown();

    thread_pool_join();
    thread_pool_deinit();

    worker_killall(instance);

    session_save_deinit();
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

    if (instance->lhttp_stock != NULL) {
        lhttp_stock_free(instance->lhttp_stock);
        instance->lhttp_stock = NULL;
    }

    if (instance->fcgi_stock != NULL) {
        fcgi_stock_free(instance->fcgi_stock);
        instance->fcgi_stock = NULL;
    }

    if (instance->was_stock != NULL) {
        hstock_free(instance->was_stock);
        instance->was_stock = NULL;
    }

    if (instance->memcached_stock != NULL)
        memcached_stock_free(instance->memcached_stock);

    if (instance->tcp_stock != NULL)
        hstock_free(instance->tcp_stock);

    if (instance->balancer != NULL)
        balancer_free(instance->balancer);

    if (instance->delegate_stock != NULL)
        hstock_free(instance->delegate_stock);

#ifdef HAVE_LIBNFS
    if (instance->nfs_cache != NULL)
        nfs_cache_free(instance->nfs_cache);

    if (instance->nfs_stock != NULL)
        nfs_stock_free(instance->nfs_stock);
#endif

    if (instance->pipe_stock != NULL)
        stock_free(instance->pipe_stock);

    local_control_handler_deinit(instance);
    global_control_handler_deinit(instance);

    fb_pool_disable();

    pool_commit();
}

static void
reload_event_callback(int fd gcc_unused, short event gcc_unused,
                      void *ctx)
{
    struct instance *instance = (struct instance*)ctx;

    daemon_log(3, "caught signal %d, flushing all caches (pid=%d)\n",
               fd, (int)getpid());

    daemonize_reopen_logfile();

    translate_cache_flush(instance->translate_cache);
    http_cache_flush(instance->http_cache);
    filter_cache_flush(instance->filter_cache);
    fb_pool_compress();
}

void
init_signals(struct instance *instance)
{
    signal(SIGPIPE, SIG_IGN);

    shutdown_listener_init(&instance->shutdown_listener,
                           shutdown_callback, instance);

    event_set(&instance->sighup_event, SIGHUP, EV_SIGNAL|EV_PERSIST,
              reload_event_callback, instance);
    event_add(&instance->sighup_event, NULL);
}

void
deinit_signals(struct instance *instance)
{
    shutdown_listener_deinit(&instance->shutdown_listener);
    event_del(&instance->sighup_event);
}

static void
add_listener(struct instance *instance, struct addrinfo *ai)
{
    GError *error = NULL;

    assert(ai != NULL);

    do {
        listener_node *node = (listener_node *)
            p_malloc(instance->pool, sizeof(*node));

        node->listener = listener_new(instance->pool, ai->ai_family, ai->ai_socktype,
                                      ai->ai_protocol, ai->ai_addr,
                                      ai->ai_addrlen,
                                      &http_listener_handler, instance,
                                      &error);
        if (node->listener == NULL) {
            fprintf(stderr, "%s\n", error->message);
            g_error_free(error);
            exit(2);
        }

        list_add(&node->siblings, &instance->listeners);

        ai = ai->ai_next;
    } while (ai != NULL);
}

static void
add_tcp_listener(struct instance *instance, int port)
{
    listener_node *node = (listener_node *)
        p_malloc(instance->pool, sizeof(*node));
    GError *error = NULL;

    node->listener = listener_tcp_port_new(instance->pool, port,
                                           &http_listener_handler, instance,
                                           &error);
    if (node->listener == NULL) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        exit(2);
    }

    list_add(&node->siblings, &instance->listeners);
}

int main(int argc, char **argv)
{
    int ret;
    bool bret;
    int gcc_unused ref;

    static struct instance instance;
    instance.config.session_cookie = "beng_proxy_session";
    instance.config.session_cookie = "beng_proxy_session";
    instance.config.session_idle_timeout = 1200;
    instance.config.max_connections = 8192;
    instance.config.http_cache_size = 512 * 1024 * 1024;
    instance.config.filter_cache_size = 128 * 1024 * 1024;
#ifdef HAVE_LIBNFS
    instance.config.nfs_cache_size = 256 * 1024 * 1024;
#endif
    instance.config.translate_cache_size = 131072;
    instance.config.fcgi_stock_max_idle = 16;
    instance.config.was_stock_max_idle = 16;

#ifndef NDEBUG
    if (geteuid() != 0)
        debug_mode = true;
#endif

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

    ssl_global_init();
    ssl_client_init();

    direct_global_init();

    instance.event_base = event_init();
    fb_pool_init(true);

    list_init(&instance.listeners);
    list_init(&instance.connections);
    list_init(&instance.workers);

    init_signals(&instance);

    children_init(instance.pool);

    if (!crash_global_init()) {
        fprintf(stderr, "crash_global_init() failed\n");
        return EXIT_FAILURE;
    }

    bret = session_manager_init(instance.config.session_idle_timeout,
                                instance.config.cluster_size,
                                instance.config.cluster_node);
    if (!bret) {
        fprintf(stderr, "session_manager_init() failed\n");
        exit(2);
    }

    session_save_init(instance.config.session_save_path);

    for (unsigned i = 0; i < instance.config.num_ports; ++i)
        add_tcp_listener(&instance, instance.config.ports[i]);

    for (unsigned i = 0; i < instance.config.num_listen; ++i)
        add_listener(&instance, instance.config.listen[i]);

    if (!global_control_handler_init(instance.pool, &instance))
        exit(2);

    local_control_handler_init(&instance);
    local_control_handler_open(&instance);

    instance.balancer = balancer_new(instance.pool);
    instance.tcp_stock = tcp_stock_new(instance.pool,
                                       instance.config.tcp_stock_limit);
    instance.tcp_balancer = tcp_balancer_new(instance.pool, instance.tcp_stock,
                                             instance.balancer);

    if (instance.config.memcached_server != NULL)
        instance.memcached_stock =
            memcached_stock_new(instance.pool, instance.tcp_balancer,
                                instance.config.memcached_server);

    if (instance.config.translation_socket != NULL) {
        struct tstock *translate_stock =
            tstock_new(instance.pool, instance.tcp_stock,
                       instance.config.translation_socket);

        instance.translate_cache = translate_cache_new(instance.pool, translate_stock,
                                                       instance.config.translate_cache_size);
    }

    instance.lhttp_stock = lhttp_stock_new(instance.pool, 0, 16);

    instance.fcgi_stock = fcgi_stock_new(instance.pool,
                                         instance.config.fcgi_stock_limit,
                                         instance.config.fcgi_stock_max_idle);

    instance.was_stock = was_stock_new(instance.pool,
                                       instance.config.was_stock_limit,
                                       instance.config.was_stock_max_idle);

    instance.delegate_stock = delegate_stock_new(instance.pool);

#ifdef HAVE_LIBNFS
    instance.nfs_stock = nfs_stock_new(instance.pool);
    instance.nfs_cache = nfs_cache_new(instance.pool,
                                       instance.config.nfs_cache_size,
                                       instance.nfs_stock);
#endif

    instance.resource_loader = resource_loader_new(instance.pool,
                                                   instance.tcp_balancer,
                                                   instance.lhttp_stock,
                                                   instance.fcgi_stock,
                                                   instance.was_stock,
                                                   instance.delegate_stock,
                                                   instance.nfs_cache);

    instance.http_cache = http_cache_new(instance.pool,
                                         instance.config.http_cache_size,
                                         instance.memcached_stock,
                                         instance.resource_loader);

    instance.pipe_stock = pipe_stock_new(instance.pool);
    instance.filter_cache = filter_cache_new(instance.pool,
                                             instance.config.filter_cache_size,
                                             instance.resource_loader);

    failure_init(instance.pool);
    bulldog_init(instance.config.bulldog_path);

    global_translate_cache = instance.translate_cache;
    global_tcp_stock = instance.tcp_stock;
    global_tcp_balancer = instance.tcp_balancer;
    global_memcached_stock = instance.memcached_stock;
    global_http_cache = instance.http_cache;
    global_lhttp_stock = instance.lhttp_stock;
    global_fcgi_stock = instance.fcgi_stock;
    global_was_stock = instance.was_stock;
    global_delegate_stock = instance.delegate_stock;
    global_nfs_stock = instance.nfs_stock;
    global_nfs_cache = instance.nfs_cache;
    global_filter_cache = instance.filter_cache;
    global_pipe_stock = instance.pipe_stock;

    /* daemonize */

    ret = daemonize();
    if (ret < 0)
        exit(2);

    /* launch the access logger */

    if (!log_global_init(instance.config.access_logger))
        return EXIT_FAILURE;

    /* daemonize II */

    if (daemon_user_defined(&instance.config.user))
        capabilities_pre_setuid();

    if (daemon_user_set(&instance.config.user) < 0)
        return EXIT_FAILURE;

    if (daemon_user_defined(&instance.config.user))
        capabilities_post_setuid();

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
    } else {
        instance_fork_cow(&instance, false);
    }

    /* main loop */

    event_dispatch();

    /* cleanup */

    log_global_deinit();

    bulldog_deinit();
    failure_deinit();

    free_all_listeners(&instance);

    fb_pool_deinit();

    event_base_free(instance.event_base);

    tpool_deinit();
    ref = pool_unref(instance.pool);
    assert(ref == 0);
    pool_commit();

    pool_recycler_clear();

    ssl_client_deinit();
    ssl_global_deinit();

    crash_global_deinit();

    daemonize_cleanup();

    direct_global_deinit();

    ua_classification_deinit();
}
