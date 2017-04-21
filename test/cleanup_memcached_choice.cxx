#include "PInstance.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "balancer.hxx"
#include "stock/MapStock.hxx"
#include "address_list.hxx"
#include "memcached/memcached_stock.hxx"
#include "http_cache_choice.hxx"
#include "lease.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "direct.hxx"
#include "fb_pool.hxx"
#include "system/SetupProcess.hxx"
#include "net/AddressInfo.hxx"
#include "net/Resolver.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

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

int
main(int argc, char **argv)
try {
    if (argc != 3) {
        fprintf(stderr, "usage: cleanup-memcached-choice HOST[:PORT] URI\n");
        return 1;
    }

    /* initialize */

    SetupProcess();

    direct_global_init();
    const ScopeFbPoolInit fb_pool_init;

    PInstance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test", 8192);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_STREAM;

    const auto address_info = Resolve(argv[1], 11211, &hints);
    const AddressList address_list(ShallowCopy(), address_info);

    auto *tcp_stock = tcp_stock_new(instance.event_loop, 0);
    TcpBalancer *tcp_balancer = tcp_balancer_new(*tcp_stock,
                                                 *balancer_new(instance.event_loop));
    auto *stock = memcached_stock_new(instance.event_loop, *tcp_balancer,
                                      address_list);

    /* send memcached request */

    CancellablePointer cancel_ptr;
    http_cache_choice_cleanup(*pool, *stock, argv[2],
                              cleanup_callback, nullptr,
                              cancel_ptr);

    pool_unref(pool);
    pool_commit();

    instance.event_loop.Dispatch();

    tcp_balancer_free(tcp_balancer);
    delete tcp_stock;

    /* cleanup */

    return EXIT_SUCCESS;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
