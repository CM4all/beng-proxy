/*
 * Objects in stock.  May be used for connection pooling.
 *
 * The 'stock' class holds a number of idle objects.  The 'hstock'
 * class is a hash table of any number of 'stock' objects, each with a
 * different URI.  The URI may be something like a hostname:port pair
 * for HTTP client connections - it is not used by this class, but
 * passed to the stock_class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STOCK_H
#define __BENG_STOCK_H

#include "pool.h"
#include "list.h"

struct stock_item {
    struct list_head list_head;
    struct stock *stock;
    int is_idle;
};

struct stock_class {
    size_t item_size;

    int (*create)(void *ctx, struct stock_item *item, const char *uri);
    int (*validate)(void *ctx, struct stock_item *item);
    void (*destroy)(void *ctx, struct stock_item *item);
};


/* stock.c */

struct stock;

struct stock *
stock_new(pool_t pool, const struct stock_class *class,
          void *class_ctx, const char *uri);

void
stock_free(struct stock **stock_r);

struct stock_item *
stock_get(struct stock *stock);

void
stock_put(struct stock_item *item, int destroy);

void
stock_del(struct stock_item *item);

static inline int
stock_item_is_idle(const struct stock_item *item)
{
    return item->is_idle;
}


/* hstock.c */

struct hstock;

struct hstock *
hstock_new(pool_t pool, const struct stock_class *class, void *class_ctx);

void
hstock_free(struct hstock **hstock_r);

struct stock_item *
hstock_get(struct hstock *hstock, const char *uri);

void
hstock_put(struct hstock *hstock, const char *uri, struct stock_item *item,
           int destroy);


#endif
