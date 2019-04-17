/*
 * Copyright 2007-2019 Content Management AG
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

#pragma once

#include "BalancerMap.hxx"
#include "address_list.hxx"
#include "paddress.hxx"
#include "pool/pool.hxx"
#include "net/SocketAddress.hxx"
#include "net/FailureManager.hxx"
#include "util/Cancellable.hxx"

#include <utility>

class CancellablePointer;

/**
 * Generic connection balancer.
 */
template<class R>
class BalancerRequest final : Cancellable {
    R request;

    struct pool &pool;

    BalancerMap &balancer;

    const AddressList &address_list;

    CancellablePointer cancel_ptr;

    /**
     * The "sticky id" of the incoming HTTP request.
     */
    const sticky_hash_t session_sticky;

    /**
     * The number of remaining connection attempts.  We give up when
     * we get an error and this attribute is already zero.
     */
    unsigned retries;

    FailurePtr failure;

public:
    template<typename... Args>
    BalancerRequest(struct pool &_pool,
                    BalancerMap &_balancer,
                    const AddressList &_address_list,
                    CancellablePointer &_cancel_ptr,
                    sticky_hash_t _session_sticky,
                    Args&&... args) noexcept
        :request(std::forward<Args>(args)...),
         pool(_pool), balancer(_balancer),
         address_list(_address_list),
         session_sticky(_session_sticky),
         retries(CalculateRetries(address_list))
    {
        _cancel_ptr = *this;
    }

    BalancerRequest(const BalancerRequest &) = delete;

    void Destroy() noexcept {
        this->~BalancerRequest();
    }

private:
    void Cancel() noexcept override {
        cancel_ptr.Cancel();
        Destroy();
    }

    static unsigned CalculateRetries(const AddressList &address_list) noexcept {
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

public:
    static constexpr BalancerRequest &Cast(R &r) noexcept {
        return ContainerCast(r, &BalancerRequest::request);
    }

    void Next(Expiry now) noexcept {
        const SocketAddress address =
            balancer.Get(now, address_list, session_sticky);

        /* we need to copy this address because it may come from
           the balancer's cache, and the according cache item may
           be flushed at any time */
        const auto current_address = DupAddress(pool, address);
        failure = balancer.GetFailureManager().Make(current_address);

        request.Send(pool, current_address, cancel_ptr);
    }

    void ConnectSuccess() noexcept {
        failure->UnsetConnect();
    }

    bool ConnectFailure(Expiry now) noexcept {
        failure->SetConnect(now, std::chrono::seconds(20));

        if (retries-- > 0){
            /* try again, next address */
            Next(now);
            return true;
        } else
            /* give up */
            return false;
    }

    template<typename... Args>
    static void Start(struct pool &pool, Expiry now,
                      Args&&... args) noexcept {
        auto r = NewFromPool<BalancerRequest>(pool, pool,
                                              std::forward<Args>(args)...);
        r->Next(now);
    }
};
