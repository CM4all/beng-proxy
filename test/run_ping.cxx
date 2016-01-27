#include "ping.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "async.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/Parser.hxx"
#include "event/Base.hxx"
#include "util/Error.hxx"

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

    RootPool root_pool;
    LinearPool pool(root_pool, "test", 8192);

    Error error;
    const auto address = ParseSocketAddress(argv[1], 0, false, error);
    if (address.IsNull()) {
        fprintf(stderr, "%s\n", error.GetMessage());
        return EXIT_FAILURE;
    }

    EventBase event_base;

    ping(pool, address,
         &my_ping_handler, nullptr,
         &my_async_ref);

    event_base.Dispatch();

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
