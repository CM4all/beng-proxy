/*
 * Objects in stock.  May be used for connection pooling.
 *
 * The 'stock' class holds a number of idle objects.  The URI may be
 * something like a hostname:port pair for HTTP client connections -
 * it is not used by this class, but passed to the stock_class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOCK_HXX
#define BENG_PROXY_STOCK_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>
#include <inline/list.h>

#include <stddef.h>

struct pool;
struct async_operation_ref;
struct stock_item;
struct stock;

struct stock_handler {
    /**
     * The stock has become empty.  It is safe to delete it from
     * within this method.
     */
    void (*empty)(struct stock *stock, const char *uri, void *ctx);
};

struct stock_get_handler {
    void (*ready)(struct stock_item *item, void *ctx);
    void (*error)(GError *error, void *ctx);
};

struct stock_item {
    struct list_head siblings;
    struct stock *stock;
    struct pool *pool;

#ifndef NDEBUG
    bool is_idle;
#endif

    const struct stock_get_handler *handler;
    void *handler_ctx;
};

struct stock_class {
    size_t item_size;

    struct pool *(*pool)(void *ctx, struct pool *parent, const char *uri);
    void (*create)(void *ctx, struct stock_item *item,
                   const char *uri, void *info,
                   struct pool *caller_pool,
                   struct async_operation_ref *async_ref);
    bool (*borrow)(void *ctx, struct stock_item *item);
    void (*release)(void *ctx, struct stock_item *item);
    void (*destroy)(void *ctx, struct stock_item *item);
};

struct stock_stats {
    unsigned busy, idle;
};

struct stock;

/**
 * @param handler optional handler class
 */
struct stock *
stock_new(struct pool *pool, const struct stock_class *_class,
          void *class_ctx, const char *uri, unsigned limit, unsigned max_idle,
          const struct stock_handler *handler, void *handler_ctx);

void
stock_free(struct stock *stock);

gcc_pure
const char *
stock_get_uri(struct stock *stock);

/**
 * Returns true if there are no items in the stock - neither idle nor
 * busy.
 */
gcc_pure
bool
stock_is_empty(const struct stock *stock);

/**
 * Obtain statistics.
 */
gcc_pure
void
stock_add_stats(const struct stock *stock, struct stock_stats *data);

void
stock_get(struct stock *stock, struct pool *pool, void *info,
          const struct stock_get_handler *handler, void *handler_ctx,
          struct async_operation_ref *async_ref);

/**
 * Obtains an item from the stock without going through the callback.
 * This requires a stock class which finishes the create() method
 * immediately.
 */
gcc_pure
struct stock_item *
stock_get_now(struct stock *stock, struct pool *pool, void *info, GError **error_r);

void
stock_item_available(struct stock_item *item);

void
stock_item_failed(struct stock_item *item, GError *error);

void
stock_item_aborted(struct stock_item *item);

void
stock_put(struct stock_item *item, bool destroy);

void
stock_del(struct stock_item *item);

#endif
