/*
 * NFS connection manager.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_STOCK_HXX
#define BENG_PROXY_NFS_STOCK_HXX

#include "glibfwd.hxx"

struct pool;
struct NfsStock;
struct NfsClient;
struct async_operation_ref;
class EventLoop;

struct NfsStockGetHandler {
    void (*ready)(NfsClient *client, void *ctx);
    void (*error)(GError *error, void *ctx);
};

NfsStock *
nfs_stock_new(EventLoop &event_loop, struct pool &pool);

void
nfs_stock_free(NfsStock *stock);

void
nfs_stock_get(NfsStock *stock, struct pool *pool,
              const char *server, const char *export_name,
              const NfsStockGetHandler *handler, void *ctx,
              struct async_operation_ref *async_ref);

#endif
