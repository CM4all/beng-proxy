#include "client_balancer.hxx"
#include "net/ConnectSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "PInstance.hxx"
#include "pool.hxx"
#include "AllocatorPtr.hxx"
#include "balancer.hxx"
#include "failure.hxx"
#include "address_list.hxx"
#include "net/Resolver.hxx"
#include "net/AddressInfo.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Context final : PInstance, ConnectSocketHandler {
    Balancer *const balancer;

    enum {
        NONE, SUCCESS, TIMEOUT, ERROR,
    } result = TIMEOUT;

    UniqueSocketDescriptor fd;
    std::exception_ptr error;

    Context()
        :balancer(balancer_new(event_loop))
    {
    }

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
try {
    if (argc <= 1) {
        fprintf(stderr, "Usage: run-client-balancer ADDRESS ...\n");
        return EXIT_FAILURE;
    }

    /* initialize */

    Context ctx;

    LinearPool pool(ctx.root_pool, "test", 8192);
    AllocatorPtr alloc(pool);

    const ScopeFailureInit failure;

    AddressList address_list;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_STREAM;

    for (int i = 1; i < argc; ++i)
        address_list.Add(alloc, Resolve(argv[i], 80, &hints));

    /* connect */

    CancellablePointer cancel_ptr;
    client_balancer_connect(ctx.event_loop, *pool, *ctx.balancer,
                            false, SocketAddress::Null(),
                            0, &address_list, 30,
                            ctx, cancel_ptr);

    ctx.event_loop.Dispatch();

    assert(ctx.result != Context::NONE);

    /* cleanup */

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
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
