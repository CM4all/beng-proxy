/*
 * High level NFS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_request.h"
#include "nfs_cache.h"
#include "http-response.h"
#include "static-headers.h"
#include "strmap.h"
#include "pool.h"

#include <sys/stat.h>

struct nfs_request {
    struct pool *pool;

    const char *path;
    const char *content_type;

    struct http_response_handler_ref handler;

    struct async_operation_ref *async_ref;
};

static void
nfs_request_error(GError *error, void *ctx)
{
    struct nfs_request *r = ctx;

    http_response_handler_invoke_abort(&r->handler, error);
}

/*
 * nfs_stock_get_handler
 *
 */

static void
nfs_request_response(struct nfs_cache_handle *handle,
                     const struct stat *st, void *ctx)
{
    struct nfs_request *r = ctx;

    struct strmap *headers = strmap_new(r->pool, 16);
    static_response_headers(r->pool, headers, -1, st,
                            r->content_type);
    strmap_add(headers, "cache-control", "max-age=60");

    struct istream *body = nfs_cache_handle_open(r->pool, handle,
                                                 0, st->st_size);

    http_response_handler_invoke_response(&r->handler,
                                          // TODO: handle revalidation etc.
                                          HTTP_STATUS_OK,
                                          headers,
                                          body);
}

static const struct nfs_cache_handler nfs_request_cache_handler = {
    .response = nfs_request_response,
    .error = nfs_request_error,
};

/*
 * constructor
 *
 */

void
nfs_request(struct pool *pool, struct nfs_cache *nfs_cache,
            const char *server, const char *export, const char *path,
            const char *content_type,
            const struct http_response_handler *handler, void *handler_ctx,
            struct async_operation_ref *async_ref)
{
    struct nfs_request *r = p_malloc(pool, sizeof(*r));

    r->pool = pool;
    r->path = path;
    r->content_type = content_type;
    http_response_handler_set(&r->handler, handler, handler_ctx);
    r->async_ref = async_ref;

    nfs_cache_request(pool, nfs_cache, server, export, path,
                      &nfs_request_cache_handler, r,
                      async_ref);
}
