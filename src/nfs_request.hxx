/*
 * High level NFS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_REQUEST_HXX
#define BENG_PROXY_NFS_REQUEST_HXX

struct pool;
struct NfsCache;
class HttpResponseHandler;
struct async_operation_ref;

void
nfs_request(struct pool &pool, NfsCache &nfs_cache,
            const char *server, const char *export_, const char *path,
            const char *content_type,
            HttpResponseHandler &handler,
            struct async_operation_ref *async_ref);

#endif
