/*
 * Handle proxying of widget contents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "proxy-widget.h"
#include "request.h"
#include "header-writer.h"
#include "header-forward.h"
#include "http-server.h"
#include "http-util.h"
#include "global.h"

static void
widget_proxy_response(http_status_t status, struct strmap *headers,
                      istream_t body, void *ctx)
{
    struct request *request2 = ctx;
    struct http_server_request *request = request2->request;
    struct growing_buffer *headers2;

    headers = forward_response_headers(request->pool, headers,
                                       request->local_host,
                                       &request2->translate.response->response_header_forward);

    headers2 = headers_dup(request->pool, headers);

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
    struct request *request2 = ctx;
    struct http_server_request *request = request2->request;

    http_server_send_message(request, HTTP_STATUS_BAD_GATEWAY,
                             "Upstream server failed");
}

struct http_response_handler widget_proxy_handler = {
    .response = widget_proxy_response,
    .abort = widget_proxy_abort,
};
