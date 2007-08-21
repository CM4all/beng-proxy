/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "connection.h"
#include "handler.h"
#include "url-stream.h"
#include "processor.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct proxy_transfer {
    struct http_server_request *request;

    url_stream_t url_stream;
    off_t content_length;
    istream_t istream;
    int istream_eof;
};

static void
proxy_transfer_close(struct proxy_transfer *pt)
{
    if (pt->istream != NULL) {
        istream_t istream = pt->istream;
        pt->istream = NULL;
        istream_close(istream);
    }

    if (pt->url_stream != NULL) {
        url_stream_close(pt->url_stream);
        assert(pt->url_stream == NULL);
    }

    if (pt->request != NULL) {
        http_server_connection_free(&pt->request->connection);
        assert(pt->request == NULL);
    }
}

static size_t
proxy_client_istream_data(const void *data, size_t length, void *ctx)
{
    struct proxy_transfer *pt = ctx;

    /* XXX */
    if (pt->content_length == (off_t)-1)
        return http_server_send_chunk(pt->request->connection, data, length);
    else
        return http_server_send(pt->request->connection, data, length);
}

static void
proxy_client_istream_eof(void *ctx)
{
    struct proxy_transfer *pt = ctx;

    pt->istream = NULL;
    pt->istream_eof = 1;

    if (pt->request == NULL)
        return;

    if (pt->content_length == (off_t)-1)
        http_server_send_last_chunk(pt->request->connection);

    http_server_response_finish(pt->request->connection);
}

static void
proxy_client_istream_free(void *ctx)
{
    struct proxy_transfer *pt = ctx;

    if (!pt->istream_eof) {
        /* abort the transfer */
        pt->istream = NULL;
        proxy_transfer_close(pt);
    }
}

static const struct istream_handler proxy_client_istream_handler = {
    .data = proxy_client_istream_data,
    .eof = proxy_client_istream_eof,
    .free = proxy_client_istream_free,
};

static void 
proxy_http_client_callback(http_status_t status, strmap_t headers,
                           off_t content_length, istream_t body,
                           void *ctx)
{
    struct proxy_transfer *pt = ctx;
    const char *value;
    char response_headers[256];

    assert(pt->istream == NULL);

    pt->url_stream = NULL;

    if (status == 0) {
        /* XXX */
        if (!pt->istream_eof)
            proxy_transfer_close(pt);
        return;
    }

    assert(content_length >= 0);

    pt->content_length = content_length;
    pt->istream = body;

    value = strmap_get(headers, "content-type");
    if (value != NULL && strncmp(value, "text/html", 9) == 0) {
        pool_ref(pt->request->pool);

        pt->content_length = (off_t)-1;
        pt->istream = processor_new(pt->request->pool, pt->istream);

        pool_unref(pt->request->pool);
        if (pt->istream == NULL) {
            /* XXX */
            abort();
        }

        snprintf(response_headers, sizeof(response_headers),
                 "Content-Type: %s\r\nTransfer-Encoding: chunked\r\n\r\n",
                 value);
    } else {
        snprintf(response_headers, sizeof(response_headers), "Content-Length: %lu\r\n\r\n",
                 (unsigned long)content_length);
    }

    assert(pt->istream->handler == NULL);

    pt->istream->handler = &proxy_client_istream_handler;
    pt->istream->handler_ctx = pt;

    http_server_send_status(pt->request->connection, 200);
    http_server_send(pt->request->connection, response_headers, strlen(response_headers));
    http_server_try_write(pt->request->connection);    
}

static const char *const copy_headers[] = {
    "user-agent",
    NULL
};

static size_t proxy_response_body(struct http_server_request *request,
                                  void *buffer, size_t max_length)
{
    struct proxy_transfer *pt = request->handler_ctx;

    (void)buffer;
    (void)max_length;

    istream_read(pt->istream);

    return 0;
}

static void proxy_response_free(struct http_server_request *request)
{
    struct proxy_transfer *pt = request->handler_ctx;

    assert(request == pt->request);

    request->handler_ctx = NULL;
    pt->request = NULL;

    proxy_transfer_close(pt);
}

static const struct http_server_request_handler proxy_request_handler = {
    .response_body = proxy_response_body,
    .free = proxy_response_free,
};

void
proxy_callback(struct client_connection *connection,
               struct http_server_request *request,
               struct translated *translated)
{
    struct proxy_transfer *pt;

    (void)connection;

    if (request->method != HTTP_METHOD_HEAD &&
        request->method != HTTP_METHOD_GET) {
        http_server_send_message(request->connection,
                                 HTTP_STATUS_METHOD_NOT_ALLOWED,
                                 "This method is not supported.");
        http_server_response_finish(request->connection);
        return;
    }

    pt = p_calloc(request->pool, sizeof(*pt));
    pt->request = request;

    pt->url_stream = url_stream_new(request->pool,
                                    HTTP_METHOD_GET, translated->path, NULL,
                                    proxy_http_client_callback, pt);
    if (pt->url_stream == NULL) {
        http_server_send_message(request->connection,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        http_server_response_finish(request->connection);
        return;
    }

    request->handler = &proxy_request_handler;
    request->handler_ctx = pt;
}
