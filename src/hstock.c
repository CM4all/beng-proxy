/*
 * Objects in stock.  May be used for connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "stock.h"
#include "hashmap.h"

#include <daemon/log.h>

#include <event.h>

#include <assert.h>

struct hstock {
    pool_t pool;
    const struct stock_class *class;
    void *class_ctx;

    struct hashmap *stocks;

    struct event cleanup_event;
};

static void
hstock_schedule_cleanup(struct hstock *hstock)
{
    static const struct timeval tv = { .tv_sec = 120, .tv_usec = 0 };

    evtimer_add(&hstock->cleanup_event, &tv);
}

static bool
hstock_match_empty_stock(const char *key, void *value, void *ctx)
{
    struct hstock *hstock = ctx;
    struct stock *stock = value;

    if (stock_is_empty(stock)) {
        daemon_log(5, "hstock(%p) remove empty stock(%p, '%s')\n",
                   (const void *)hstock, (const void *)stock, key);

        stock_free(stock);
        return true;
    } else
        return false;
}

static void
hstock_cleanup_event_callback(int fd __attr_unused, short event __attr_unused,
                              void *ctx)
{
    struct hstock *hstock = ctx;

    daemon_log(6, "hstock_cleanup_event_callback(%p)\n", (const void *)hstock);

    hashmap_remove_all_match(hstock->stocks, hstock_match_empty_stock, hstock);
    hstock_schedule_cleanup(hstock);
}

struct hstock *
hstock_new(pool_t pool, const struct stock_class *class, void *class_ctx)
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
    hstock->stocks = hashmap_new(pool, 64);

    evtimer_set(&hstock->cleanup_event, hstock_cleanup_event_callback, hstock);
    hstock_schedule_cleanup(hstock);

    return hstock;
}

void
hstock_free(struct hstock *hstock)
{
    const struct hashmap_pair *pair;

    assert(hstock != NULL);

    evtimer_del(&hstock->cleanup_event);

    hashmap_rewind(hstock->stocks);

    while ((pair = hashmap_next(hstock->stocks)) != NULL) {
        struct stock *stock = (struct stock *)pair->value;

        stock_free(stock);
    }

    pool_unref(hstock->pool);
}

void
hstock_get(struct hstock *hstock, pool_t pool,
           const char *uri, void *info,
           stock_callback_t callback, void *callback_ctx,
           struct async_operation_ref *async_ref)
{
    struct stock *stock;

    assert(hstock != NULL);

    stock = (struct stock *)hashmap_get(hstock->stocks, uri);

    if (stock == NULL) {
        stock = stock_new(hstock->pool, hstock->class, hstock->class_ctx, uri);
        hashmap_set(hstock->stocks, p_strdup(hstock->pool, uri), stock);
    }

    stock_get(stock, pool, info, callback, callback_ctx, async_ref);
}

void
hstock_put(struct hstock *hstock __attr_unused, const char *uri __attr_unused,
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
