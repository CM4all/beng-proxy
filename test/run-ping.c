#include "ping.h"
#include "pool.h"
#include "async.h"
#include "address-string.h"
#include "address-envelope.h"

#include <event.h>
#include <stdio.h>
#include <stdlib.h>

static bool success;
static struct async_operation_ref my_async_ref;

static void
my_ping_response(G_GNUC_UNUSED void *ctx)
{
    success = true;
    printf("ok\n");
}

static void
my_ping_timeout(G_GNUC_UNUSED void *ctx)
{
    fprintf(stderr, "timeout\n");
}

static void
my_ping_error(GError *error, G_GNUC_UNUSED void *ctx)
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

    struct pool *root_pool = pool_new_libc(NULL, "root");
    struct pool *pool = pool_new_linear(root_pool, "test", 8192);

    struct address_envelope *envelope =
        address_envelope_parse(pool, argv[1], 0, false);
    if (envelope == NULL) {
        fprintf(stderr, "Could not parse IP address\n");
        pool_unref(pool);
        pool_unref(root_pool);
        return EXIT_FAILURE;
    }

    struct event_base *event_base = event_init();

    ping(pool, &envelope->address, envelope->length,
         &my_ping_handler, NULL,
         &my_async_ref);

    event_dispatch();

    pool_unref(pool);

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    event_base_free(event_base);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
