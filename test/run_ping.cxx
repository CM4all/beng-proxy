#include "ping.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "async.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/Parser.hxx"
#include "event/Base.hxx"
#include "util/Error.hxx"

#include <glib.h>

#include <stdio.h>
#include <stdlib.h>

static bool success;
static struct async_operation_ref my_async_ref;

class MyPingClientHandler final : public PingClientHandler {
public:
    void PingResponse() override {
        success = true;
        printf("ok\n");
    }

    void PingTimeout() override {
        fprintf(stderr, "timeout\n");
    }

    void PingError(GError *error) override {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
    }
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

    MyPingClientHandler handler;
    ping(pool, address,
         handler,
         &my_async_ref);

    event_base.Dispatch();

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
