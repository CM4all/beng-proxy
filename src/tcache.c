/*
 * Cache for translation server responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcache.h"
#include "cache.h"

struct tcache_item {
    pool_t pool;

    struct translate_response response;
};

struct tcache {
    pool_t pool;

    struct cache *cache;

    struct stock *stock;
};


/*
 * cache class
 *
 */

static int
tcache_validate(struct cache_item *item __attr_unused)
{
    return 1;
}

static void
tcache_destroy(struct cache_item *_item)
{
    struct tcache_item *item = (struct tcache_item *)_item;

    pool_unref(item->pool);
}

static const struct cache_class tcache_class = {
    .validate = tcache_validate,
    .destroy = tcache_destroy,
};


/*
 * constructor
 *
 */

struct tcache *
translate_cache_new(pool_t pool, struct stock *translate_stock)
{
    struct tcache *tcache = p_malloc(pool, sizeof(*tcache));

    tcache->pool = pool;
    tcache->cache = cache_new(pool, &tcache_class, 1024);
    tcache->stock = translate_stock;

    return tcache;
}


/*
 * methods
 *
 */

void
translate_cache(pool_t pool, struct tcache *tcache,
                const struct translate_request *request,
                translate_callback_t callback,
                void *ctx,
                struct async_operation_ref *async_ref)
{
    translate(pool, tcache->stock, request, callback, ctx, async_ref);
}
