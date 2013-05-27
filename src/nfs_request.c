/*
 * High level NFS client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_request.h"
#include "nfs_stock.h"
#include "nfs_client.h"
#include "istream_nfs.h"
#include "istream.h"
#include "http-response.h"
#include "static-headers.h"
#include "strmap.h"
#include "pool.h"

#include <sys/stat.h>

struct nfs_request {
    struct pool *pool;

    const char *path;

    struct http_response_handler_ref handler;

    struct async_operation_ref *async_ref;
};

/*
 * nfs_client_open_file_handler
 *
 */

static void
nfs_open_ready(struct nfs_file_handle *handle, const struct stat *st,
               void *ctx)
{
    struct nfs_request *r = ctx;

    struct strmap *headers = strmap_new(r->pool, 16);
    static_response_headers(r->pool, headers, -1, st,
                            // TODO: content type from translation server
                            NULL);
    strmap_add(headers, "cache-control", "max-age=60");

    struct istream *body;
    if (st->st_size > 0) {
        body = istream_nfs_new(r->pool, handle, 0, st->st_size);
    } else {
        nfs_client_close_file(handle);
        body = istream_null_new(r->pool);
    }

    http_response_handler_invoke_response(&r->handler,
                                          // TODO: handle revalidation etc.
                                          HTTP_STATUS_OK,
                                          headers,
                                          body);
}

static void
nfs_open_error(GError *error, void *ctx)
{
    struct nfs_request *r = ctx;

    http_response_handler_invoke_abort(&r->handler, error);
}

static const struct nfs_client_open_file_handler nfs_open_handler = {
    .ready = nfs_open_ready,
    .error = nfs_open_error,
};

/*
 * nfs_stock_get_handler
 *
 */

static void
nfs_request_stock_ready(struct nfs_client *client, void *ctx)
{
    struct nfs_request *r = ctx;

    nfs_client_open_file(client, r->pool, r->path,
                         &nfs_open_handler, r, r->async_ref);
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
