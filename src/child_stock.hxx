/*
 * Launch processes and connect a stream socket to them.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_STOCK_HXX
#define BENG_PROXY_CHILD_STOCK_HXX

#include "FdType.hxx"
#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <sys/socket.h>

struct pool;
struct StockMap;
struct StockItem;
struct async_operation_ref;

struct ChildStockClass {
    /**
     * The signal that shall be used for shutting down a child
     * process.
     */
    int shutdown_signal;

    void *(*prepare)(const char *key, void *info,
                     GError **error_r);
    int (*socket_type)(const char *key, void *info, void *ctx);
    int (*clone_flags)(const char *key, void *info, int flags, void *ctx);
    int (*run)(const char *key, void *info, void *ctx);
    void (*free)(void *ctx);
};

StockMap *
child_stock_new(struct pool *pool, unsigned limit, unsigned max_idle,
                const ChildStockClass *cls);

const char *
child_stock_item_key(const StockItem *item);

/**
 * Connect a socket to the given child process.  The socket must be
 * closed before the #stock_item is returned.
 *
 * @return a socket descriptor or -1 on error
 */
int
child_stock_item_connect(const StockItem *item,
                         GError **error_r);

gcc_pure
static inline FdType
child_stock_item_get_type(gcc_unused const StockItem *item)
{
    return FdType::FD_SOCKET;
}

/**
 * Wrapper for hstock_put().
 */
void
child_stock_put(StockMap *hstock, StockItem *item,
                bool destroy);

#endif
