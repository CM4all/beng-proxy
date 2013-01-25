/*
 * Load balancer for struct uri_with_address.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "balancer.h"
#include "cache.h"
#include "address_list.h"
#include "address_envelope.h"
#include "failure.h"
#include "bulldog.h"

#include <assert.h>
#include <stdbool.h>
#include <time.h>

struct balancer_item {
    struct cache_item item;

    struct pool *pool;

    /** the index of the item that will be returned next */
    unsigned next;

    struct address_list addresses;
};

struct balancer {
    struct pool *pool;

    /**
     * This library uses the cache library to store remote host
     * states in a lossy way.
     */
    struct cache *cache;
};

static bool
check_failure(const struct address_envelope *envelope, bool allow_fade)
{
    enum failure_status status = failure_get_status(&envelope->address,
                                                    envelope->length);
    if (status == FAILURE_FADE && allow_fade)
        status = FAILURE_OK;
    return status == FAILURE_OK;
}

gcc_pure
static bool
check_bulldog(const struct address_envelope *envelope, bool allow_fade)
{
    return bulldog_check(&envelope->address, envelope->length) &&
        (allow_fade ||
         !bulldog_is_fading(&envelope->address, envelope->length));
}

static bool
check_envelope(const struct address_envelope *envelope, bool allow_fade)
{
    return check_failure(envelope, allow_fade) &&
        check_bulldog(envelope, allow_fade);
}

static const struct address_envelope *
next_failover_address(const struct address_list *list)
{
    assert(list->size > 0);

    for (unsigned i = 0; i < list->size; ++i) {
        const struct address_envelope *envelope = list->addresses[i];
        if (check_envelope(envelope, true))
            return envelope;
    }

    /* none available - return first node as last resort */
    return list->addresses[0];
}

static const struct address_envelope *
next_address(struct balancer_item *item)
{
    assert(item->addresses.size >= 2);
    assert(item->next < item->addresses.size);

    const struct address_envelope *envelope =
        address_list_get_n(&item->addresses, item->next);

    ++item->next;
    if (item->next >= item->addresses.size)
        item->next = 0;

    return envelope;
}

static const struct address_envelope *
next_address_checked(struct balancer_item *item, bool allow_fade)
{
    const struct address_envelope *first = next_address(item);
    if (first == NULL)
        return NULL;

    const struct address_envelope *ret = first;
    do {
        if (check_envelope(ret, allow_fade))
            return ret;

        ret = next_address(item);
        assert(ret != NULL);
    } while (ret != first);

    /* all addresses failed: */
    return first;
}

static const struct address_envelope *
next_sticky_address_checked(const struct address_list *al, unsigned session)
{
    assert(al->size >= 2);

    unsigned i = session % al->size;
    bool allow_fade = true;

    const struct address_envelope *first = address_list_get_n(al, i);
    assert(first != NULL);
    const struct address_envelope *ret = first;
    do {
        if (check_envelope(ret, allow_fade))
            return ret;

        /* only the first iteration is allowed to override
           FAILURE_FADE */
        allow_fade = false;

        ++i;
        if (i >= al->size)
            i = 0;

        ret = address_list_get_n(al, i);

    } while (ret != first);

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
    struct balancer_item *item = (struct balancer_item *)_item;

    pool_unref(item->pool);
}

static const struct cache_class balancer_cache_class = {
    .destroy = balancer_cache_destroy,
};


/*
 * public API
 *
 */

struct balancer *
balancer_new(struct pool *pool)
{
    struct balancer *balancer = p_malloc(pool, sizeof(*balancer));

    balancer->pool = pool;
    balancer->cache = cache_new(pool, &balancer_cache_class,
                                1021, 2048);
    return balancer;
}

void
balancer_free(struct balancer *balancer)
{
    cache_close(balancer->cache);
}

const struct address_envelope *
balancer_get(struct balancer *balancer, const struct address_list *list,
             unsigned session)
{
    const char *key;
    struct balancer_item *item;
    struct pool *pool;

    if (address_list_is_single(list))
        return address_list_first(list);

    switch (list->sticky_mode) {
    case STICKY_NONE:
        break;

    case STICKY_FAILOVER:
        return next_failover_address(list);

    case STICKY_SOURCE_IP:
    case STICKY_SESSION_MODULO:
    case STICKY_COOKIE:
    case STICKY_JVM_ROUTE:
        if (session != 0)
            return next_sticky_address_checked(list, session);
        break;
    }

    key = address_list_key(list);
    item = (struct balancer_item *)cache_get(balancer->cache, key);

    if (item == NULL) {
        /* create a new cache item */

        pool = pool_new_linear(balancer->pool, "balancer_item", 1024);
        item = p_malloc(pool, sizeof(*item));
        cache_item_init(&item->item, time(NULL) + 1800, 1);
        item->pool = pool;
        item->next = 0;
        address_list_copy(pool, &item->addresses, list);

        cache_put(balancer->cache, p_strdup(pool, key), &item->item);
    }

    return next_address_checked(item, list->sticky_mode == STICKY_NONE);
}

void
balancer_event_add(struct balancer *balancer)
{
    cache_event_add(balancer->cache);
}

void
balancer_event_del(struct balancer *balancer)
{
    cache_event_del(balancer->cache);
}
