/*
 * The 'hstock' class is a hash table of any number of 'stock'
 * objects, each with a different URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "hstock.h"
#include "stock.h"
#include "hashmap.h"
#include "pool.h"

#include <daemon/log.h>

#include <assert.h>

struct hstock {
    struct pool *pool;
    const struct stock_class *class;
    void *class_ctx;

    /**
     * The maximum number of items in each stock.
     */
    unsigned limit;

    /**
     * The maximum number of permanent idle items in each stock.
     */
    unsigned max_idle;

    struct hashmap *stocks;
};

/*
 * stock handler
 *
 */

static void
hstock_stock_empty(struct stock *stock, const char *uri, void *ctx)
{
    struct hstock *hstock = ctx;

    daemon_log(5, "hstock(%p) remove empty stock(%p, '%s')\n",
               (const void *)hstock, (const void *)stock, uri);
    hashmap_remove_existing(hstock->stocks, uri, stock);

    stock_free(stock);
}

static const struct stock_handler hstock_stock_handler = {
    .empty = hstock_stock_empty,
};

struct hstock *
hstock_new(struct pool *pool, const struct stock_class *class, void *class_ctx,
           unsigned limit, unsigned max_idle)
{
    struct hstock *hstock;

    assert(pool != NULL);
    assert(class != NULL);
    assert(class->item_size > sizeof(struct stock_item));
    assert(class->create != NULL);
    assert(class->borrow != NULL);
    assert(class->release != NULL);
    assert(class->destroy != NULL);
    assert(max_idle > 0);

    pool = pool_new_linear(pool, "hstock", 4096);
    hstock = p_malloc(pool, sizeof(*hstock));
    hstock->pool = pool;
    hstock->class = class;
    hstock->class_ctx = class_ctx;
    hstock->limit = limit;
    hstock->max_idle = max_idle;
    hstock->stocks = hashmap_new(pool, 64);

    return hstock;
}

void
hstock_free(struct hstock *hstock)
{
    const struct hashmap_pair *pair;

    assert(hstock != NULL);

    hashmap_rewind(hstock->stocks);

    while ((pair = hashmap_next(hstock->stocks)) != NULL) {
        struct stock *stock = (struct stock *)pair->value;

        stock_free(stock);
    }

    pool_unref(hstock->pool);
}

void
hstock_add_stats(const struct hstock *stock, struct stock_stats *data)
{
    struct hashmap *h = stock->stocks;
    hashmap_rewind(h);

    const struct hashmap_pair *p;
    while ((p = hashmap_next(h)) != NULL) {
        const struct stock *s = (const struct stock *)p->value;
        stock_add_stats(s, data);
    }
}

static struct stock *
hstock_get_stock(struct hstock *hstock, const char *uri)
{
    assert(hstock != NULL);

    struct stock *stock = (struct stock *)hashmap_get(hstock->stocks, uri);
    if (stock == NULL) {
        stock = stock_new(hstock->pool, hstock->class, hstock->class_ctx, uri,
                          hstock->limit, hstock->max_idle,
                          &hstock_stock_handler, hstock);
        hashmap_set(hstock->stocks, stock_get_uri(stock), stock);
    }

    return stock;
}

void
hstock_get(struct hstock *hstock, struct pool *pool,
           const char *uri, void *info,
           const struct stock_get_handler *handler, void *handler_ctx,
           struct async_operation_ref *async_ref)
{
    assert(hstock != NULL);

    struct stock *stock = hstock_get_stock(hstock, uri);
    stock_get(stock, pool, info, handler, handler_ctx, async_ref);
}

void
hstock_put(struct hstock *hstock gcc_unused, const char *uri gcc_unused,
           struct stock_item *object, bool destroy)
{
#ifndef NDEBUG
    struct stock *stock = (struct stock *)hashmap_get(hstock->stocks, uri);

    assert(stock != NULL);
    assert(object != NULL);
    assert(stock == object->stock);
#endif

    stock_put(object, destroy);
}
