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

#include "balancer.hxx"
#include "address_list.hxx"
#include "net/SocketAddress.hxx"
#include "failure.hxx"
#include "bulldog.hxx"

#include <string>

#include <assert.h>

static bool
check_failure(const SocketAddress address, bool allow_fade)
{
    enum failure_status status = failure_get_status(address);
    if (status == FAILURE_FADE && allow_fade)
        status = FAILURE_OK;
    return status == FAILURE_OK;
}

gcc_pure
static bool
check_bulldog(const SocketAddress address, bool allow_fade)
{
    return bulldog_check(address) &&
        (allow_fade || !bulldog_is_fading(address));
}

static bool
CheckAddress(const SocketAddress address, bool allow_fade)
{
    return check_failure(address, allow_fade) &&
        check_bulldog(address, allow_fade);
}

static SocketAddress
next_failover_address(const AddressList &list)
{
    assert(list.GetSize() > 0);

    for (auto i : list)
        if (CheckAddress(i, true))
            return i;

    /* none available - return first node as last resort */
    return list[0];
}

const SocketAddress &
Balancer::Item::NextAddress(const AddressList &addresses)
{
    assert(addresses.GetSize() >= 2);
    assert(next < addresses.GetSize());

    const SocketAddress &address = addresses[next];

    ++next;
    if (next >= addresses.GetSize())
        next = 0;

    return address;
}

const SocketAddress &
Balancer::Item::NextAddressChecked(const AddressList &addresses,
                                   bool allow_fade)
{
    const auto &first = NextAddress(addresses);
    const SocketAddress *ret = &first;
    do {
        if (CheckAddress(*ret, allow_fade))
            return *ret;

        ret = &NextAddress(addresses);
    } while (ret != &first);

    /* all addresses failed: */
    return first;
}

static const SocketAddress &
next_sticky_address_checked(const AddressList &al, sticky_hash_t sticky_hash)
{
    assert(al.GetSize() >= 2);

    unsigned i = sticky_hash % al.GetSize();
    bool allow_fade = true;

    const SocketAddress &first = al[i];
    const SocketAddress *ret = &first;
    do {
        if (CheckAddress(*ret, allow_fade))
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
Balancer::Get(const AddressList &list, sticky_hash_t sticky_hash) noexcept
{
    if (list.IsSingle())
        return list[0];

    switch (list.sticky_mode) {
    case StickyMode::NONE:
        break;

    case StickyMode::FAILOVER:
        return next_failover_address(list);

    case StickyMode::SOURCE_IP:
    case StickyMode::HOST:
    case StickyMode::XHOST:
    case StickyMode::SESSION_MODULO:
    case StickyMode::COOKIE:
    case StickyMode::JVM_ROUTE:
        if (sticky_hash != 0)
            return next_sticky_address_checked(list, sticky_hash);
        break;
    }

    const char *key = list.GetKey();
    auto *item = (Item *)cache.Get(key);

    if (item == nullptr) {
        /* create a new cache item */
        item = new Item(key);
        cache.Put(item->key.c_str(), *item);
    }

    return item->NextAddressChecked(list,
                                    list.sticky_mode == StickyMode::NONE);
}
