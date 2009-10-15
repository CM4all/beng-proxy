/*
 * Handle proxying of widget contents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "proxy-widget.h"
#include "header-writer.h"
#include "http-server.h"
#include "http-util.h"
#include "growing-buffer.h"
#include "global.h"

/*
 * processor_env.response_handler
 *
 */

static void
widget_proxy_response(http_status_t status, struct strmap *headers,
                      istream_t body, void *ctx)
{
    struct http_server_request *request = ctx;
    struct growing_buffer *headers2;
    static const char *const copy_headers[] = {
        "age",
        "etag",
        "content-encoding",
        "content-language",
        "content-md5",
        "content-range",
        "content-type",
        "content-disposition",
        "last-modified",
        "location",
        "retry-after",
        "vary",
        NULL,
    };

    headers2 = growing_buffer_new(request->pool, 2048);

    if (headers != NULL)
        headers_copy(headers, headers2, copy_headers);

#ifndef NO_DEFLATE
    if (body != NULL && istream_available(body, false) == (off_t)-1 &&
        (headers == NULL || strmap_get(headers, "content-encoding") == NULL) &&
        http_client_accepts_encoding(request->headers, "deflate")) {
        header_write(headers2, "content-encoding", "deflate");
        body = istream_deflate_new(request->pool, body);
    } else
#endif
#ifdef SPLICE
    if (body != NULL)
        body = istream_pipe_new(request->pool, body, global_pipe_stock);
#else
    {}
#endif

    http_server_response(request, status, headers2, body);
}

static void
widget_proxy_abort(void *ctx)
{
    struct http_server_request *request = ctx;

    http_server_send_message(request, HTTP_STATUS_BAD_GATEWAY,
                             "Upstream server failed");
}

struct http_response_handler widget_proxy_handler = {
    .response = widget_proxy_response,
    .abort = widget_proxy_abort,
};
