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
#include "bulldog.hxx"
#include "AllocatorPtr.hxx"

#include <string>

#include <assert.h>
#include <time.h>

struct Balancer {
    struct Item final : CacheItem {
        const std::string key;

        /** the index of the item that will be returned next */
        unsigned next = 0;

        explicit Item(const char *_key)
            :CacheItem(std::chrono::minutes(30), 1),
             key(_key) {}

        const SocketAddress &NextAddress(const AddressList &addresses);
        const SocketAddress &NextAddressChecked(const AddressList &addresses,
                                                bool allow_fade);

        /* virtual methods from class CacheItem */
        void Destroy() override {
            delete this;
        }
    };

    /**
     * This library uses the cache library to store remote host
     * states in a lossy way.
     */
    Cache cache;

    explicit Balancer(EventLoop &event_loop)
        :cache(event_loop, 1021, 2048) {}
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
balancer_new(EventLoop &event_loop)
{
    return new Balancer(event_loop);
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
    if (list.IsSingle())
        return list[0];

    switch (list.sticky_mode) {
    case StickyMode::NONE:
        break;

    case StickyMode::FAILOVER:
        return next_failover_address(list);

    case StickyMode::SOURCE_IP:
    case StickyMode::HOST:
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
        item = new Balancer::Item(key);
        balancer.cache.Put(item->key.c_str(), *item);
    }

    return item->NextAddressChecked(list,
                                    list.sticky_mode == StickyMode::NONE);
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
