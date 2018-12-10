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

/*
 * Wrapper for the tcp_stock class to support load balancing.
 */

#ifndef BENG_PROXY_TCP_BALANCER_HXX
#define BENG_PROXY_TCP_BALANCER_HXX

#include "StickyHash.hxx"
#include "balancer.hxx"
#include "event/Chrono.hxx"

struct pool;
struct AddressList;
class TcpStock;
class StockGetHandler;
struct StockItem;
class CancellablePointer;
class SocketAddress;

class TcpBalancer {
    friend struct TcpBalancerRequest;

    TcpStock &tcp_stock;

    Balancer balancer;

public:
    /**
     * @param tcp_stock the underlying #TcpStock object
     */
    TcpBalancer(TcpStock &_tcp_stock, FailureManager &failure_manager)
        :tcp_stock(_tcp_stock), balancer(failure_manager) {}

    FailureManager &GetFailureManager() {
        return balancer.GetFailureManager();
    }

    /**
     * @param session_sticky a portion of the session id that is used to
     * select the worker; 0 means disable stickiness
     * @param timeout the connect timeout for each attempt
     */
    void Get(struct pool &pool,
             bool ip_transparent,
             SocketAddress bind_address,
             sticky_hash_t session_sticky,
             const AddressList &address_list,
             Event::Duration timeout,
             StockGetHandler &handler,
             CancellablePointer &cancel_ptr);
};

#endif
