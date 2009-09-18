/*
 * Load balancer for struct uri_with_address.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "balancer.h"
#include "cache.h"
#include "uri-address.h"
#include "failure.h"
#include "bulldog.h"

#include <assert.h>
#include <stdbool.h>
#include <time.h>

struct balancer_item {
    struct cache_item item;

    pool_t pool;

    struct uri_with_address *uwa;
};

struct balancer {
    pool_t pool;

    /**
     * This library uses the cache library to store remote host
     * states in a lossy way.
     */
    struct cache *cache;
};

static const struct sockaddr *
uri_address_next_checked(struct uri_with_address *uwa, socklen_t *addrlen_r)
{
    const struct sockaddr *first = uri_address_next(uwa, addrlen_r), *ret = first;
    if (first == NULL)
        return NULL;

    do {
        if (!failure_check(ret, *addrlen_r) &&
            bulldog_check(ret, *addrlen_r))
            return ret;

        ret = uri_address_next(uwa, addrlen_r);
        assert(ret != NULL);
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
balancer_new(pool_t pool)
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

const struct sockaddr *
balancer_get(struct balancer *balancer,
             const struct uri_with_address *uwa, socklen_t *address_size_r)
{
    const char *key;
    struct balancer_item *item;
    pool_t pool;

    if (uri_address_is_single(uwa))
        return uri_address_first(uwa, address_size_r);

    key = uri_address_key(uwa);
    item = (struct balancer_item *)cache_get(balancer->cache, key);

    if (item == NULL) {
        /* create a new cache item */

        pool = pool_new_linear(balancer->pool, "balancer_item", 512);
        item = p_malloc(pool, sizeof(*item));
        cache_item_init(&item->item, time(NULL) + 1800, 1);
        item->pool = pool;
        item->uwa = uri_address_dup(pool, uwa);

        cache_put(balancer->cache, p_strdup(pool, key), &item->item);
    }

    return uri_address_next_checked(item->uwa, address_size_r);
}
