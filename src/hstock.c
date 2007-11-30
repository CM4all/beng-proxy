/*
 * Objects in stock.  May be used for connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "stock.h"
#include "hashmap.h"

#include <assert.h>

struct hstock {
    pool_t pool;
    const struct stock_class *class;
    void *class_ctx;

    hashmap_t stocks;
};

struct hstock *
hstock_new(pool_t pool, const struct stock_class *class, void *class_ctx)
{
    struct hstock *hstock;

    assert(pool != NULL);
    assert(class != NULL);
    assert(class->item_size > sizeof(struct stock_item));
    assert(class->create != NULL);
    assert(class->validate != NULL);
    assert(class->destroy != NULL);

    pool = pool_new_linear(pool, "hstock", 1024);
    hstock = p_malloc(pool, sizeof(*hstock));
    hstock->pool = pool;
    hstock->class = class;
    hstock->class_ctx = class_ctx;
    hstock->stocks = hashmap_new(pool, 64);

    return hstock;
}

void
hstock_free(struct hstock **hstock_r)
{
    struct hstock *hstock;
    const struct hashmap_pair *pair;

    assert(hstock_r != NULL);
    assert(*hstock_r != NULL);

    hstock = *hstock_r;
    *hstock_r = NULL;

    hashmap_rewind(hstock->stocks);

    while ((pair = hashmap_next(hstock->stocks)) != NULL) {
        struct stock *stock = (struct stock *)pair->value;

        stock_free(&stock);
    }

    pool_unref(hstock->pool);
}

struct stock_item *
hstock_get(struct hstock *hstock, const char *uri)
{
    struct stock *stock;

    assert(hstock != NULL);

    stock = (struct stock *)hashmap_get(hstock->stocks, uri);

    if (stock == NULL) {
        stock = stock_new(hstock->pool, hstock->class, hstock->class_ctx, uri);
        hashmap_put(hstock->stocks, uri, stock, 1);
    }

    return stock_get(stock);
}

void
hstock_put(struct hstock *hstock, const char *uri, struct stock_item *object, int destroy)
{
    struct stock *stock = (struct stock *)hashmap_get(hstock->stocks, uri);

    assert(stock != NULL);
    assert(object != NULL);

    stock_put(stock, object, destroy);
}
