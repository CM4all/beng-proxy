/*
 * Load balancer for AddressList.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "balancer.hxx"
#include "cache.hxx"
#include "address_list.hxx"
#include "net/SocketAddress.hxx"
#include "failure.hxx"
#include "bulldog.h"
#include "pool.hxx"

#include <assert.h>
#include <stdbool.h>
#include <time.h>

struct Balancer {
    struct Item final : CacheItem {
        struct pool *const pool;

        /** the index of the item that will be returned next */
        unsigned next = 0;

        AddressList addresses;

        Item(struct pool &_pool, const AddressList &_addresses)
            :CacheItem(std::chrono::minutes(30), 1),
             pool(&_pool), addresses(_pool, _addresses) {
        }

        /* virtual methods from class CacheItem */
        void Destroy() override {
            pool_unref(pool);
        }
    };

    struct pool *pool;

    /**
     * This library uses the cache library to store remote host
     * states in a lossy way.
     */
    Cache cache;

    Balancer(struct pool &_pool, EventLoop &event_loop)
        :pool(&_pool),
         cache(_pool, event_loop, 1021, 2048) {}
};

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
    return bulldog_check(address.GetAddress(), address.GetSize()) &&
        (allow_fade ||
         !bulldog_is_fading(address.GetAddress(), address.GetSize()));
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

static const SocketAddress &
next_address(Balancer::Item *item)
{
    assert(item->addresses.GetSize() >= 2);
    assert(item->next < item->addresses.GetSize());

    const SocketAddress &address = item->addresses[item->next];

    ++item->next;
    if (item->next >= item->addresses.GetSize())
        item->next = 0;

    return address;
}

static const SocketAddress &
next_address_checked(Balancer::Item *item, bool allow_fade)
{
    const SocketAddress &first = next_address(item);
    const SocketAddress *ret = &first;
    do {
        if (CheckAddress(*ret, allow_fade))
            return *ret;

        ret = &next_address(item);
    } while (ret != &first);

    /* all addresses failed: */
    return first;
}

static const SocketAddress &
next_sticky_address_checked(const AddressList &al, unsigned session)
{
    assert(al.GetSize() >= 2);

    unsigned i = session % al.GetSize();
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

/*
 * public API
 *
 */

Balancer *
balancer_new(struct pool &pool, EventLoop &event_loop)
{
    return new Balancer(pool, event_loop);
}

void
balancer_free(Balancer *balancer)
{
    delete balancer;
}

SocketAddress
balancer_get(Balancer &balancer, const AddressList &list,
             unsigned session)
{
    struct pool *pool;

    if (list.IsSingle())
        return list[0];

    switch (list.sticky_mode) {
    case StickyMode::NONE:
        break;

    case StickyMode::FAILOVER:
        return next_failover_address(list);

    case StickyMode::SOURCE_IP:
    case StickyMode::SESSION_MODULO:
    case StickyMode::COOKIE:
    case StickyMode::JVM_ROUTE:
        if (session != 0)
            return next_sticky_address_checked(list, session);
        break;
    }

    const char *key = list.GetKey();
    auto *item = (Balancer::Item *)balancer.cache.Get(key);

    if (item == nullptr) {
        /* create a new cache item */

        pool = pool_new_linear(balancer.pool, "balancer_item", 1024);
        item = NewFromPool<Balancer::Item>(*pool, *pool, list);

        balancer.cache.Put(p_strdup(pool, key), *item);
    }

    return next_address_checked(item, list.sticky_mode == StickyMode::NONE);
}

void
balancer_event_add(Balancer &balancer)
{
    balancer.cache.EventAdd();
}

void
balancer_event_del(Balancer &balancer)
{
    balancer.cache.EventDel();
}
