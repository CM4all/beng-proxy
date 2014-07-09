/*
 * Launch processes and connect a stream socket to them.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_STOCK_HXX
#define BENG_PROXY_CHILD_STOCK_HXX

#include "istream-direct.h"
#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <sys/socket.h>

struct pool;
struct hstock;
struct stock_item;
struct stock_get_handler;
struct async_operation_ref;

struct child_stock_class {
    /**
     * The signal that shall be used for shutting down a child
     * process.
     */
    int shutdown_signal;

    void *(*prepare)(struct pool *pool, const char *key, void *info,
                     GError **error_r);
    int (*clone_flags)(const char *key, void *info, int flags, void *ctx);
    int (*run)(struct pool *pool, const char *key, void *info, void *ctx);
    void (*free)(void *ctx);
};

struct hstock *
child_stock_new(struct pool *pool, unsigned limit, unsigned max_idle,
                const struct child_stock_class *cls);

const char *
child_stock_item_key(const struct stock_item *item);

/**
 * Connect a socket to the given child process.  The socket must be
 * closed before the #stock_item is returned.
 *
 * @return a socket descriptor or -1 on error
 */
int
child_stock_item_connect(const struct stock_item *item,
                         GError **error_r);

gcc_pure
static inline enum istream_direct
child_stock_item_get_type(gcc_unused const struct stock_item *item)
{
    return ISTREAM_SOCKET;
}

/**
 * Wrapper for hstock_put().
 */
void
child_stock_put(struct hstock *hstock, struct stock_item *item,
                bool destroy);

#endif
