/*
 * Objects in stock.  May be used for connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "stock.h"
#include "list.h"

#include <assert.h>

struct stock_item {
    struct list_head list_head;
    void *object;
};

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

        stock->class->destroy(stock->class_ctx, item->object);
    }

    pool_unref(stock->pool);
}

void *
stock_get(struct stock *stock)
{
    while (stock->num_idle > 0) {
        struct stock_item *item = (struct stock_item *)stock->idle.next;

        assert(!list_empty(&stock->idle));

        list_remove(&item->list_head);
        --stock->num_idle;

        if (stock->class->validate(stock->class_ctx, item->object))
            return item->object;
        else
            stock->class->destroy(stock->class_ctx, item->object);
    }

    return stock->class->create(stock->class_ctx, stock->uri);
}

void
stock_put(struct stock *stock, void *object, int destroy)
{
    (void)destroy;

    if (destroy || stock->num_idle >= 8 ||
        !stock->class->validate(stock->class_ctx, object)) {
        stock->class->destroy(stock->class_ctx, object);
    } else {
        struct stock_item *item = p_malloc(stock->pool, sizeof(*item));
        item->object = object;
        list_add(&item->list_head, &stock->idle);
        ++stock->num_idle;
    }
}
