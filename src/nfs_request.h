/*
 * High level NFS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_REQUEST_H
#define BENG_PROXY_NFS_REQUEST_H

struct pool;
struct nfs_stock;
struct http_response_handler;
struct async_operation_ref;

void
nfs_request(struct pool *pool, struct nfs_stock *nfs_stock,
            const char *server, const char *export, const char *path,
            const struct http_response_handler *handler, void *handler_ctx,
            struct async_operation_ref *async_ref);

#endif
