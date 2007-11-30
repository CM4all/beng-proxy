/*
 * Objects in stock.  May be used for connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "stock.h"

#include <assert.h>

struct stock {
    pool_t pool;
    const struct stock_class *class;
    void *class_ctx;
    const char *uri;

    unsigned num_idle;
    struct list_head idle;
};

struct stock *
stock_new(pool_t pool, const struct stock_class *class,
          void *class_ctx, const char *uri)
{
    struct stock *stock;

    assert(class->item_size > sizeof(struct stock_item));

    pool = pool_new_linear(pool, "stock", 1024);
    stock = p_malloc(pool, sizeof(*stock));
    stock->pool = pool;
    stock->class = class;
    stock->class_ctx = class_ctx;
    stock->uri = uri;
    stock->num_idle = 0;
    list_init(&stock->idle);

    return stock;
}

void
stock_free(struct stock **stock_r)
{
    struct stock *stock = *stock_r;
    *stock_r = NULL;

    while (stock->num_idle > 0) {
        struct stock_item *item = (struct stock_item *)stock->idle.next;

        assert(!list_empty(&stock->idle));

        list_remove(&item->list_head);
        --stock->num_idle;

        stock->class->destroy(stock->class_ctx, item);
    }

    pool_unref(stock->pool);
}

struct stock_item *
stock_get(struct stock *stock)
{
    struct stock_item *item;
    int ret;

    while (stock->num_idle > 0) {
        assert(!list_empty(&stock->idle));

        item = (struct stock_item *)stock->idle.next;
        list_remove(&item->list_head);
        --stock->num_idle;

        if (stock->class->validate(stock->class_ctx, item))
            return item;
        else
            stock->class->destroy(stock->class_ctx, item);
    }

    item = p_malloc(stock->pool, stock->class->item_size);
    ret = stock->class->create(stock->class_ctx, item, stock->uri);
    if (!ret) {
        p_free(stock->pool, item);
        return NULL;
    }

    return item;
}

void
stock_put(struct stock *stock, struct stock_item *item, int destroy)
{
    (void)destroy;

    if (destroy || stock->num_idle >= 8 ||
        !stock->class->validate(stock->class_ctx, item)) {
        stock->class->destroy(stock->class_ctx, item);
    } else {
        list_add(&item->list_head, &stock->idle);
        ++stock->num_idle;
    }
}
