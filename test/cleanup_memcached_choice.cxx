/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>

static void
cleanup_callback(std::exception_ptr ep, gcc_unused void *ctx)
{
    if (ep)
        PrintException(ep);
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

    TcpStock tcp_stock(instance.event_loop, 0);
    Balancer balancer;
    TcpBalancer *tcp_balancer = tcp_balancer_new(tcp_stock, balancer);
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

    /* cleanup */

    return EXIT_SUCCESS;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
