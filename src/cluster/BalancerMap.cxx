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

#include "BalancerMap.hxx"
#include "address_list.hxx"
#include "net/SocketAddress.hxx"
#include "net/FailureManager.hxx"

#include <assert.h>

static bool
CheckAddress(FailureManager &failure_manager, Expiry now,
             const SocketAddress address, bool allow_fade) noexcept
{
    return failure_manager.Check(now, address, allow_fade);
}

static SocketAddress
next_failover_address(FailureManager &failure_manager, Expiry now,
                      const AddressList &list) noexcept
{
    assert(list.GetSize() > 0);

    for (auto i : list)
        if (CheckAddress(failure_manager, now, i, true))
            return i;

    /* none available - return first node as last resort */
    return list[0];
}

static const SocketAddress &
next_sticky_address_checked(FailureManager &failure_manager, const Expiry now,
                            const AddressList &al,
                            sticky_hash_t sticky_hash) noexcept
{
    assert(al.GetSize() >= 2);

    unsigned i = sticky_hash % al.GetSize();
    bool allow_fade = true;

    const SocketAddress &first = al[i];
    const SocketAddress *ret = &first;
    do {
        if (CheckAddress(failure_manager, now, *ret, allow_fade))
            return *ret;

        /* only the first iteration is allowed to override
           FAILURE_FADE */
        allow_fade = false;

        ++i;
        if (i >= al.GetSize())
            i = 0;

        ret = &al[i];

    } while (ret != &first);

    /* all addresses failed: */
    return first;
}

SocketAddress
BalancerMap::Get(const Expiry now,
                 const AddressList &list, sticky_hash_t sticky_hash) noexcept
{
    if (list.IsSingle())
        return list[0];

    switch (list.sticky_mode) {
    case StickyMode::NONE:
        break;

    case StickyMode::FAILOVER:
        return next_failover_address(failure_manager, now, list);

    case StickyMode::SOURCE_IP:
    case StickyMode::HOST:
    case StickyMode::XHOST:
    case StickyMode::SESSION_MODULO:
    case StickyMode::COOKIE:
    case StickyMode::JVM_ROUTE:
        if (sticky_hash != 0)
            return next_sticky_address_checked(failure_manager, now, list,
                                               sticky_hash);
        break;
    }

    std::string key = list.GetKey();
    auto *item = cache.Get(key);

    if (item == nullptr)
        /* create a new cache item */
        item = &cache.Put(std::move(key), RoundRobinBalancer());

    return item->Get(failure_manager, now, list,
                     list.sticky_mode == StickyMode::NONE);
}
