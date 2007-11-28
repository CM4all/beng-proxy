/*
 * Objects in stock.  May be used for connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STOCK_H
#define __BENG_STOCK_H

#include "pool.h"

struct stock_class {
    void *(*create)(void *ctx, const char *uri);
    int (*validate)(void *ctx, void *object);
    void (*destroy)(void *ctx, void *object);
};


/* stock.c */

struct stock;

struct stock *
stock_new(pool_t pool, const struct stock_class *class,
          void *class_ctx, const char *uri);

void
stock_free(struct stock **stock_r);

void *
stock_get(struct stock *stock);

void
stock_put(struct stock *stock, void *object, int destroy);


/* hstock.c */

struct hstock;

struct hstock *
hstock_new(pool_t pool, const struct stock_class *class, void *class_ctx);

void
hstock_free(struct hstock **hstock_r);

void *
hstock_get(struct hstock *hstock, const char *uri);

void
hstock_put(struct hstock *hstock, const char *uri, void *object, int destroy);


#endif
