/*
 * NFS connection manager.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_STOCK_H
#define BENG_PROXY_NFS_STOCK_H

#include <glib.h>

struct pool;
struct nfs_stock;
struct nfs_client;
struct async_operation_ref;

struct nfs_stock_get_handler {
    void (*ready)(struct nfs_client *client,
                  void *ctx);
    void (*error)(GError *error, void *ctx);
};

struct nfs_stock *
nfs_stock_new(struct pool *pool);

void
nfs_stock_free(struct nfs_stock *stock);

void
nfs_stock_get(struct nfs_stock *stock, struct pool *pool,
              const char *server, const char *export,
              const struct nfs_stock_get_handler *handler, void *ctx,
              struct async_operation_ref *async_ref);

#endif
