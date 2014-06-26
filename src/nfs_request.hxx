/*
 * High level NFS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_REQUEST_HXX
#define BENG_PROXY_NFS_REQUEST_HXX

struct pool;
struct nfs_cache;
struct http_response_handler;
struct async_operation_ref;

void
nfs_request(struct pool &pool, struct nfs_cache *nfs_cache,
            const char *server, const char *export_, const char *path,
            const char *content_type,
            const struct http_response_handler *handler, void *handler_ctx,
            struct async_operation_ref *async_ref);

#endif
