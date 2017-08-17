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
 * Generic connection balancer.
 */

#ifndef GENERIC_BALANCER_HXX
#define GENERIC_BALANCER_HXX

#include "balancer.hxx"
#include "failure.hxx"
#include "address_list.hxx"
#include "pool.hxx"
#include "net/SocketAddress.hxx"

#include <utility>

class CancellablePointer;

template<class R>
struct BalancerRequest : R {
    struct pool &pool;

    Balancer &balancer;

    const AddressList &address_list;

    CancellablePointer &cancel_ptr;

    /**
     * The "sticky id" of the incoming HTTP request.
     */
    const sticky_hash_t session_sticky;

    /**
     * The number of remaining connection attempts.  We give up when
     * we get an error and this attribute is already zero.
     */
    unsigned retries;

    SocketAddress current_address;

    template<typename... Args>
    BalancerRequest(struct pool &_pool,
                    Balancer &_balancer,
                    const AddressList &_address_list,
                    CancellablePointer &_cancel_ptr,
                    sticky_hash_t _session_sticky,
                    Args&&... args)
        :R(std::forward<Args>(args)...),
         pool(_pool), balancer(_balancer),
         address_list(_address_list),
         cancel_ptr(_cancel_ptr),
         session_sticky(_session_sticky),
         retries(CalculateRetries(address_list)) {}

    BalancerRequest(const BalancerRequest &) = delete;

    static unsigned CalculateRetries(const AddressList &address_list) {
        const unsigned size = address_list.GetSize();
        if (size <= 1)
            return 0;
        else if (size == 2)
            return 1;
        else if (size == 3)
            return 2;
        else
            return 3;
    }

    static constexpr BalancerRequest &Cast(R &r) {
        return (BalancerRequest &)r;
    }

    const SocketAddress &GetAddress() const {
        return current_address;
    }

    void Next() {
        const SocketAddress address =
            balancer_get(balancer, address_list, session_sticky);

        /* we need to copy this address because it may come from
           the balancer's cache, and the according cache item may
           be flushed at any time */
        const struct sockaddr *new_address = (const struct sockaddr *)
            p_memdup(&pool, address.GetAddress(), address.GetSize());
        current_address = { new_address, address.GetSize() };

        R::Send(pool, current_address, cancel_ptr);
    }

    void Success() {
        failure_unset(current_address, FAILURE_FAILED);
    }

    bool Failure() {
        failure_add(current_address);

        if (retries-- > 0){
            /* try again, next address */
            Next();
            return true;
        } else
            /* give up */
            return false;
    }

    template<typename... Args>
    static void Start(struct pool &pool,
                      Args&&... args) {
        auto r = NewFromPool<BalancerRequest>(pool, pool,
                                              std::forward<Args>(args)...);
        r->Next();
    }
};

#endif
