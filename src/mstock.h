/*
 * A wrapper for #stock that allows multiple users of one stock_item.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_MSTOCK_H
#define BENG_PROXY_MSTOCK_H

#include <inline/compiler.h>
#include <inline/list.h>

#include <glib.h>

#include <stdbool.h>

struct pool;
struct async_operation_ref;
struct hstock;
struct lease;
struct lease_ref;
struct stock_item;
struct stock_stats;
struct stock_get_handler;

#ifdef __cplusplus
extern "C" {
#endif

gcc_malloc
struct mstock *
mstock_new(struct pool *pool, struct hstock *hstock);

void
mstock_free(struct mstock *mstock);

/**
 * Obtain statistics.
 */
gcc_pure
void
mstock_add_stats(const struct mstock *stock, struct stock_stats *data);

/**
 * Obtains an item from the mstock without going through the callback.
 * This requires a stock class which finishes the create() method
 * immediately.
 */
gcc_pure
struct stock_item *
mstock_get_now(struct mstock *mstock, struct pool *caller_pool,
               const char *uri, void *info,
               struct lease_ref *lease_ref,
               GError **error_r);


#ifdef __cplusplus
}
#endif

#endif
