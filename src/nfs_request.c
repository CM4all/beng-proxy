/*
 * High level NFS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_request.h"
#include "nfs_stock.h"
#include "nfs_client.h"
#include "http-response.h"
#include "pool.h"

struct nfs_request {
    struct pool *pool;

    const char *path;

    struct http_response_handler_ref handler;

    struct async_operation_ref *async_ref;
};

/*
 * nfs_stock_get_handler
 *
 */

static void
nfs_request_stock_ready(struct nfs_client *client, void *ctx)
{
    struct nfs_request *r = ctx;

    nfs_client_get_file(client, r->pool, r->path,
                        r->handler.handler, r->handler.ctx, r->async_ref);
}

static void
nfs_request_stock_error(GError *error, void *ctx)
{
    struct nfs_request *r = ctx;

    http_response_handler_invoke_abort(&r->handler, error);
}

static const struct nfs_stock_get_handler nfs_request_stock_handler = {
    .ready = nfs_request_stock_ready,
    .error = nfs_request_stock_error,
};

/*
 * constructor
 *
 */

void
nfs_request(struct pool *pool, struct nfs_stock *nfs_stock,
            const char *server, const char *export, const char *path,
            const struct http_response_handler *handler, void *handler_ctx,
            struct async_operation_ref *async_ref)
{
    struct nfs_request *r = p_malloc(pool, sizeof(*r));

    r->pool = pool;
    r->path = path;
    http_response_handler_set(&r->handler, handler, handler_ctx);
    r->async_ref = async_ref;

    nfs_stock_get(nfs_stock, pool, server, export,
                  &nfs_request_stock_handler, r,
                  async_ref);
}
