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
class NfsClient;
class CancellablePointer;
class EventLoop;

class NfsStockGetHandler {
public:
    virtual void OnNfsStockReady(NfsClient &client) = 0;
    virtual void OnNfsStockError(GError *error) = 0;
};

NfsStock *
nfs_stock_new(EventLoop &event_loop, struct pool &pool);

void
nfs_stock_free(NfsStock *stock);

void
nfs_stock_get(NfsStock *stock, struct pool *pool,
              const char *server, const char *export_name,
              NfsStockGetHandler &handler,
              CancellablePointer &cancel_ptr);

#endif
