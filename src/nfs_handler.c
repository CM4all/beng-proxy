/*
 * Serve HTTP requests from a NFS server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_handler.h"
#include "nfs_stock.h"
#include "nfs_client.h"
#include "file_headers.h"
#include "istream_nfs.h"
#include "istream.h"
#include "tvary.h"
#include "static-headers.h"
#include "header-writer.h"
#include "generate_response.h"
#include "growing-buffer.h"
#include "request.h"
#include "http-server.h"
#include "global.h"

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
    const struct translate_response *const tr = request2->translate.response;

    struct file_request file_request = {
        .range = RANGE_NONE,
        .skip = 0,
        .size = st->st_size,
    };

    if (!file_evaluate_request(request2, -1, st, &file_request)) {
        nfs_client_close_file(handle);
        return;
    }

    struct growing_buffer *headers = growing_buffer_new(pool, 2048);
    header_write(headers, "cache-control", "max-age=60");

    file_response_headers(headers,
                          /* TODO: Content-Type from translation server */
                          NULL,
                          -1, st,
                          request_processor_enabled(request2),
                          request_processor_first(request2));
    write_translation_vary_header(headers, request2->translate.response);

    http_status_t status = tr->status == 0 ? HTTP_STATUS_OK : tr->status;

    /* generate the Content-Range header */

    header_write(headers, "accept-ranges", "bytes");

    bool no_body = false;

    switch (file_request.range) {
    case RANGE_NONE:
        break;

    case RANGE_VALID:
        status = HTTP_STATUS_PARTIAL_CONTENT;

        header_write(headers, "content-range",
                     p_sprintf(request->pool, "bytes %lu-%lu/%lu",
                               (unsigned long)file_request.skip,
                               (unsigned long)(file_request.size - 1),
                               (unsigned long)st->st_size));
        break;

    case RANGE_INVALID:
        status = HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE;

        header_write(headers, "content-range",
                     p_sprintf(request->pool, "bytes */%lu",
                               (unsigned long)st->st_size));

        no_body = true;
        break;
    }

    struct istream *body;
    if (no_body) {
        nfs_client_close_file(handle);
        body = NULL;
    } else if (file_request.skip < file_request.size) {
        body = istream_nfs_new(pool, handle, file_request.skip,
                               file_request.size);
    } else {
        nfs_client_close_file(handle);
        body = istream_null_new(pool);
    }

    response_dispatch(request2, status, headers, body);
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
