#include "ping.hxx"
#include "pool.hxx"
#include "async.h"
#include "net/SocketAddress.hxx"
#include "net/AllocatedSocketAddress.hxx"

#include <event.h>
#include <stdio.h>
#include <stdlib.h>

static bool success;
static struct async_operation_ref my_async_ref;

static void
my_ping_response(gcc_unused void *ctx)
{
    success = true;
    printf("ok\n");
}

static void
my_ping_timeout(gcc_unused void *ctx)
{
    fprintf(stderr, "timeout\n");
}

static void
my_ping_error(GError *error, gcc_unused void *ctx)
{
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
}

static const struct ping_handler my_ping_handler = {
    .response = my_ping_response,
    .timeout = my_ping_timeout,
    .error = my_ping_error,
};

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: run-ping IP\n");
        return EXIT_FAILURE;
    }

    struct pool *root_pool = pool_new_libc(nullptr, "root");
    struct pool *pool = pool_new_linear(root_pool, "test", 8192);

    GError *error = nullptr;
    AllocatedSocketAddress address;
    if (!address.Parse(argv[1], 0, false, &error)) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        pool_unref(pool);
        pool_unref(root_pool);
        return EXIT_FAILURE;
    }

    struct event_base *event_base = event_init();

    ping(pool, address,
         &my_ping_handler, nullptr,
         &my_async_ref);

    event_dispatch();

    pool_unref(pool);

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    event_base_free(event_base);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
