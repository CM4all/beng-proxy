/*
 * The 'hstock' class is a hash table of any number of 'stock'
 * objects, each with a different URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HSTOCK_H
#define BENG_PROXY_HSTOCK_H

#include <inline/compiler.h>
#include <inline/list.h>

#include <stdbool.h>

struct pool;
struct async_operation_ref;
struct stock_class;
struct stock_item;
struct stock_stats;
struct stock_get_handler;

#ifdef __cplusplus
extern "C" {
#endif

gcc_malloc
struct hstock *
hstock_new(struct pool *pool,
           const struct stock_class *_class, void *class_ctx,
           unsigned limit, unsigned max_idle);

void
hstock_free(struct hstock *hstock);

/**
 * Obtain statistics.
 */
gcc_pure
void
hstock_add_stats(const struct hstock *stock, struct stock_stats *data);

void
hstock_get(struct hstock *hstock, struct pool *pool,
           const char *uri, void *info,
           const struct stock_get_handler *handler, void *handler_ctx,
           struct async_operation_ref *async_ref);

void
hstock_put(struct hstock *hstock, const char *uri, struct stock_item *item,
           bool destroy);


#ifdef __cplusplus
}
#endif

#endif
