#include "tcp-stock.h"
#include "balancer.h"
#include "stock.h"
#include "uri-resolver.h"
#include "memcached-stock.h"
#include "http-cache-choice.h"
#include "lease.h"
#include "async.h"
#include "socket-util.h"
#include "strref.h"
#include "strmap.h"
#include "tpool.h"
#include "serialize.h"
#include "sink-impl.h"

#include <socket/resolver.h>
#include <socket/util.h>

#include <glib.h>
#include <event.h>

#include <unistd.h>
#include <stdio.h>
#include <sys/signal.h>
#include <netdb.h>
#include <errno.h>

struct context {
    pool_t pool;

    int fd;
    bool idle, reuse;

    istream_t value;
    bool value_eof, value_abort;

    struct async_operation_ref async_ref;
};

static void
cleanup_callback(G_GNUC_UNUSED void *ctx)
{
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    struct addrinfo hints;
    struct event_base *event_base;
    pool_t root_pool;
    static struct context ctx;
    struct uri_with_address *uwa;
    struct hstock *tcp_stock;
    struct memcached_stock *stock;

    if (argc != 3) {
        fprintf(stderr, "usage: cleanup-memcached-choice HOST[:PORT] URI\n");
        return 1;
    }

    /* initialize */

    signal(SIGPIPE, SIG_IGN);

    event_base = event_init();

    root_pool = pool_new_libc(NULL, "root");
    tpool_init(root_pool);
    ctx.pool = pool_new_linear(root_pool, "test", 8192);

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    uwa = uri_address_new_resolve(ctx.pool, argv[1], 11211, &hints);
    if (uwa == NULL)
        return 1;

    tcp_stock = tcp_stock_new(ctx.pool, balancer_new(ctx.pool), 0);
    stock = memcached_stock_new(ctx.pool, tcp_stock, uwa);

    /* send memcached request */

    http_cache_choice_cleanup(ctx.pool, stock, argv[2],
                              cleanup_callback, &ctx,
                              &ctx.async_ref);

    pool_unref(ctx.pool);
    pool_commit();

    event_dispatch();

    hstock_free(tcp_stock);

    /* cleanup */

    tpool_deinit();
    pool_commit();

    pool_unref(root_pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);

    return ctx.value_eof ? 0 : 2;
}
