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

struct BalancerItem {
    struct cache_item item;

    struct pool *pool;

    /** the index of the item that will be returned next */
    unsigned next;

    AddressList addresses;
};

struct Balancer {
    struct pool *pool;

    /**
     * This library uses the cache library to store remote host
     * states in a lossy way.
     */
    struct cache *cache;
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
next_address(BalancerItem *item)
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
next_address_checked(BalancerItem *item, bool allow_fade)
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
 * cache class
 *
 */

static void
balancer_cache_destroy(struct cache_item *_item)
{
    auto &item = *(BalancerItem *)_item;

    pool_unref(item.pool);
}

static const struct cache_class balancer_cache_class = {
    .validate = nullptr,
    .destroy = balancer_cache_destroy,
};


/*
 * public API
 *
 */

Balancer *
balancer_new(struct pool &pool, EventLoop &event_loop)
{
    auto balancer = NewFromPool<Balancer>(pool);

    balancer->pool = &pool;
    balancer->cache = cache_new(pool, event_loop, balancer_cache_class,
                                1021, 2048);
    return balancer;
}

void
balancer_free(Balancer *balancer)
{
    cache_close(balancer->cache);
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
    auto *item = (BalancerItem *)cache_get(balancer.cache, key);

    if (item == nullptr) {
        /* create a new cache item */

        pool = pool_new_linear(balancer.pool, "balancer_item", 1024);
        item = NewFromPool<BalancerItem>(*pool);
        cache_item_init_relative(&item->item, 1800, 1);
        item->pool = pool;
        item->next = 0;
        item->addresses.CopyFrom(pool, list);

        cache_put(balancer.cache, p_strdup(pool, key), &item->item);
    }

    return next_address_checked(item, list.sticky_mode == StickyMode::NONE);
}

void
balancer_event_add(Balancer &balancer)
{
    cache_event_add(balancer.cache);
}

void
balancer_event_del(Balancer &balancer)
{
    cache_event_del(balancer.cache);
}
