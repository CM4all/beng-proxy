#include "client_balancer.hxx"
#include "net/ConnectSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "event/Loop.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "AllocatorPtr.hxx"
#include "balancer.hxx"
#include "failure.hxx"
#include "address_list.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <socket/resolver.h>
#include <socket/util.h>

#include <glib.h>

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

struct Context final : ConnectSocketHandler {
    Balancer *balancer;

    enum {
        NONE, SUCCESS, TIMEOUT, ERROR,
    } result = TIMEOUT;

    UniqueSocketDescriptor fd;
    std::exception_ptr error;

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(UniqueSocketDescriptor &&new_fd) override {
        result = SUCCESS;
        fd = std::move(new_fd);
        balancer_free(balancer);
    }

    void OnSocketConnectTimeout() override {
        result = TIMEOUT;
        balancer_free(balancer);
    }

    void OnSocketConnectError(std::exception_ptr ep) override {
        result = ERROR;
        error = std::move(ep);
        balancer_free(balancer);
    }
};

/*
 * main
 *
 */

int
main(int argc, char **argv)
{
    if (argc <= 1) {
        fprintf(stderr, "Usage: run-client-balancer ADDRESS ...\n");
        return EXIT_FAILURE;
    }

    /* initialize */

    EventLoop event_loop;

    RootPool root_pool;
    LinearPool pool(root_pool, "test", 8192);
    AllocatorPtr alloc(pool);

    failure_init();

    Context ctx;
    ctx.balancer = balancer_new(event_loop);

    AddressList address_list;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_STREAM;

    for (int i = 1; i < argc; ++i) {
        const char *p = argv[i];

        struct addrinfo *ai;
        int ret = socket_resolve_host_port(p, 80, &hints, &ai);
        if (ret != 0) {
            fprintf(stderr, "Failed to resolve '%s': %s\n",
                    p, gai_strerror(ret));
            return EXIT_FAILURE;
        }

        for (struct addrinfo *j = ai; j != nullptr; j = j->ai_next)
            address_list.Add(alloc, {ai->ai_addr, ai->ai_addrlen});

        freeaddrinfo(ai);
    }

    /* connect */

    CancellablePointer cancel_ptr;
    client_balancer_connect(event_loop, *pool, *ctx.balancer,
                            false, SocketAddress::Null(),
                            0, &address_list, 30,
                            ctx, cancel_ptr);

    event_loop.Dispatch();

    assert(ctx.result != Context::NONE);

    /* cleanup */

    failure_deinit();

    switch (ctx.result) {
    case Context::NONE:
        break;

    case Context::SUCCESS:
        return EXIT_SUCCESS;

    case Context::TIMEOUT:
        fprintf(stderr, "timeout\n");
        return EXIT_FAILURE;

    case Context::ERROR:
        PrintException(ctx.error);
        return EXIT_FAILURE;
    }

    assert(false);
    return EXIT_FAILURE;
}
