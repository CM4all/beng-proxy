#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "balancer.hxx"
#include "stock/MapStock.hxx"
#include "address_list.hxx"
#include "address_resolver.hxx"
#include "memcached/memcached_stock.hxx"
#include "http_cache_choice.hxx"
#include "lease.hxx"
#include "async.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "direct.hxx"
#include "fb_pool.hxx"
#include "event/Loop.hxx"
#include "system/SetupProcess.hxx"

#include <socket/resolver.h>
#include <socket/util.h>

#include <glib.h>

#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>

static void
cleanup_callback(GError *error, gcc_unused void *ctx)
{
    if (error != NULL) {
        g_printerr("%s\n", error->message);
        g_error_free(error);
    }
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    struct addrinfo hints;

    if (argc != 3) {
        fprintf(stderr, "usage: cleanup-memcached-choice HOST[:PORT] URI\n");
        return 1;
    }

    /* initialize */

    SetupProcess();

    EventLoop event_loop;

    direct_global_init();
    fb_pool_init(false);

    RootPool root_pool;
    auto *pool = pool_new_linear(root_pool, "test", 8192);

    AddressList address_list;
    address_list.Init();
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    GError *error = NULL;
    if (!address_list_resolve(pool, &address_list,
                              argv[1], 11211, &hints, &error)) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        return 1;
    }

    auto *tcp_stock = tcp_stock_new(event_loop, 0);
    TcpBalancer *tcp_balancer = tcp_balancer_new(*tcp_stock,
                                                 *balancer_new(*pool));
    auto *stock = memcached_stock_new(event_loop, tcp_balancer, &address_list);

    /* send memcached request */

    struct async_operation_ref async_ref;
    http_cache_choice_cleanup(*pool, *stock, argv[2],
                              cleanup_callback, nullptr,
                              async_ref);

    pool_unref(pool);
    pool_commit();

    event_loop.Dispatch();

    tcp_balancer_free(tcp_balancer);
    delete tcp_stock;

    /* cleanup */

    fb_pool_deinit();

    return EXIT_SUCCESS;
}
