/*
 * Objects in stock.  May be used for connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

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
    gcc_unused void *value = hashmap_remove(hstock->stocks, uri);
    assert(value == stock);

    stock_free(stock);
}

static const struct stock_handler hstock_stock_handler = {
    .empty = hstock_stock_empty,
};

struct hstock *
hstock_new(struct pool *pool, const struct stock_class *class, void *class_ctx,
           unsigned limit)
{
    struct hstock *hstock;

    assert(pool != NULL);
    assert(class != NULL);
    assert(class->item_size > sizeof(struct stock_item));
    assert(class->create != NULL);
    assert(class->borrow != NULL);
    assert(class->release != NULL);
    assert(class->destroy != NULL);

    pool = pool_new_linear(pool, "hstock", 4096);
    hstock = p_malloc(pool, sizeof(*hstock));
    hstock->pool = pool;
    hstock->class = class;
    hstock->class_ctx = class_ctx;
    hstock->limit = limit;
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
hstock_get(struct hstock *hstock, struct pool *pool,
           const char *uri, void *info,
           const struct stock_get_handler *handler, void *handler_ctx,
           struct async_operation_ref *async_ref)
{
    struct stock *stock;

    assert(hstock != NULL);

    stock = (struct stock *)hashmap_get(hstock->stocks, uri);

    if (stock == NULL) {
        stock = stock_new(hstock->pool, hstock->class, hstock->class_ctx, uri,
                          hstock->limit,
                          &hstock_stock_handler, hstock);
        hashmap_set(hstock->stocks, p_strdup(hstock->pool, uri), stock);
    }

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
