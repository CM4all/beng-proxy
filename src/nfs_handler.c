/*
 * Serve HTTP requests from a NFS server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_handler.h"
#include "nfs_stock.h"
#include "nfs_client.h"
#include "istream_nfs.h"
#include "istream.h"
#include "static-headers.h"
#include "generate_response.h"
#include "request.h"
#include "http-server.h"
#include "http-response.h"
#include "global.h"
#include "strmap.h"

#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

static void
nfs_handler_error(GError *error, void *ctx)
{
    struct request *request2 = ctx;

    response_dispatch_error(request2, error);
    g_error_free(error);
}

/*
 * nfs_client_open_file_handler
 *
 */

static void
nfs_handler_open_ready(struct nfs_file_handle *handle, const struct stat *st,
                       void *ctx)
{
    struct request *const request2 = ctx;
    struct http_server_request *const request = request2->request;
    struct pool *const pool = request->pool;

    struct strmap *headers = strmap_new(pool, 16);
    static_response_headers(pool, headers, -1, st,
                            // TODO: content type from translation server
                            NULL);
    strmap_add(headers, "cache-control", "max-age=60");

    struct istream *body;
    if (st->st_size > 0) {
        body = istream_nfs_new(pool, handle, 0, st->st_size);
    } else {
        nfs_client_close_file(handle);
        body = istream_null_new(pool);
    }

    http_response_handler_direct_response(&response_handler, request2,
                                          // TODO: handle revalidation etc.
                                          HTTP_STATUS_OK,
                                          headers,
                                          body);
}

static const struct nfs_client_open_file_handler nfs_handler_open_handler = {
    .ready = nfs_handler_open_ready,
    .error = nfs_handler_error,
};

/*
 * nfs_stock_get_handler
 *
 */

static void
nfs_handler_stock_ready(struct nfs_client *client, void *ctx)
{
    struct request *request2 = ctx;
    struct http_server_request *const request = request2->request;
    struct pool *const pool = request->pool;
    const struct translate_response *const tr = request2->translate.response;
    const struct nfs_address *const address = tr->address.u.nfs;

    nfs_client_open_file(client, pool, address->path,
                         &nfs_handler_open_handler, request2,
                         &request2->async_ref);
}

static const struct nfs_stock_get_handler nfs_handler_stock_handler = {
    .ready = nfs_handler_stock_ready,
    .error = nfs_handler_error,
};

/*
 * public
 *
 */

void
nfs_handler(struct request *request2)
{
    struct http_server_request *const request = request2->request;
    struct pool *const pool = request->pool;
    const struct translate_response *const tr = request2->translate.response;

    assert(tr != NULL);
    assert(tr->address.u.nfs != NULL);

    const struct nfs_address *const address = tr->address.u.nfs;
    assert(address->server != NULL);
    assert(address->export != NULL);
    assert(address->path != NULL);

    /* check request */

    if (request->method != HTTP_METHOD_HEAD &&
        request->method != HTTP_METHOD_GET &&
        !request2->processor_focus) {
        method_not_allowed(request2, "GET, HEAD");
        return;
    }

    /* run the delegate helper */

    nfs_stock_get(global_nfs_stock, pool, address->server, address->export,
                  &nfs_handler_stock_handler, request2,
                  &request2->async_ref);
}
