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

    assert(pool != NULL);
    assert(class != NULL);
    assert(class->item_size > sizeof(struct stock_item));
    assert(class->create != NULL);
    assert(class->validate != NULL);
    assert(class->destroy != NULL);

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
    struct stock *stock;

    assert(stock_r != NULL);
    assert(*stock_r != NULL);

    stock = *stock_r;
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

static void
destroy_item(struct stock *stock, struct stock_item *item)
{
    assert(pool_contains(item->pool, item, stock->class->item_size));

    stock->class->destroy(stock->class_ctx, item);
    p_free(item->pool, item);
}

struct async_operation *
stock_get(struct stock *stock, stock_callback_t callback, void *callback_ctx)
{
    pool_t pool;
    struct stock_item *item;

    assert(stock != NULL);

    while (stock->num_idle > 0) {
        assert(!list_empty(&stock->idle));

        item = (struct stock_item *)stock->idle.next;
        list_remove(&item->list_head);
        --stock->num_idle;

        assert(item->is_idle);

        if (stock->class->validate(stock->class_ctx, item)) {
            item->is_idle = 0;
            callback(callback_ctx, item);
            return NULL;
        }

        destroy_item(stock, item);
    }

    if (stock->class->pool == NULL)
        pool = stock->pool;
    else
        pool = stock->class->pool(stock->class_ctx, stock->pool, stock->uri);

    item = p_malloc(pool, stock->class->item_size);
    item->stock = stock;
    item->pool = pool;
    item->is_idle = 0;
    item->callback = callback;
    item->callback_ctx = callback_ctx;

    return stock->class->create(stock->class_ctx, item, stock->uri);
}

void
stock_available(struct stock_item *item, int success)
{
    struct stock *stock = item->stock;

    if (success) {
        item->callback(item->callback_ctx, item);
    } else {
        item->callback(item->callback_ctx, NULL);
        destroy_item(stock, item);
    }
}

void
stock_put(struct stock_item *item, int destroy)
{
    struct stock *stock;

    assert(item != NULL);
    assert(!item->is_idle);

    stock = item->stock;

    assert(stock != NULL);
    assert(pool_contains(item->pool, item, stock->class->item_size));

    if (destroy || stock->num_idle >= 8 ||
        !stock->class->validate(stock->class_ctx, item)) {
        destroy_item(stock, item);
    } else {
        item->is_idle = 1;
        list_add(&item->list_head, &stock->idle);
        ++stock->num_idle;
    }
}

void
stock_del(struct stock_item *item)
{
    struct stock *stock;

    assert(item != NULL);
    assert(item->is_idle);

    stock = item->stock;

    assert(stock != NULL);
    assert(stock->num_idle > 0);
    assert(!list_empty(&stock->idle));
    assert(pool_contains(item->pool, item, stock->class->item_size));
    assert(item->list_head.next->prev == &item->list_head);
    assert(item->list_head.prev->next == &item->list_head);

    list_remove(&item->list_head);
    --stock->num_idle;

    destroy_item(stock, item);
}
