/*
 * Utilities for transforming the HTTP response being sent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.h"
#include "http-server.h"
#include "header-writer.h"
#include "url-stream.h"
#include "widget.h"
#include "embed.h"
#include "frame.h"
#include "http-util.h"
#include "proxy-widget.h"
#include "session.h"
#include "filter.h"

static const char *const copy_headers[] = {
    "age",
    "etag",
    "content-encoding",
    "content-language",
    "content-md5",
    "content-range",
    "content-type",
    "last-modified",
    "retry-after",
    "vary",
    NULL,
};

static const char *const copy_headers_processed[] = {
    "etag",
    "content-language",
    "content-type",
    "vary",
    NULL,
};


static void
response_close(struct request *request)
{
    assert(request != NULL);
    assert(request->request != NULL);
    assert(request->request->pool != NULL);

    if (async_ref_defined(&request->url_stream))
        async_abort(&request->url_stream);

    if (!request->response_sent)
        http_server_send_message(request->request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
}

static const char *
request_absolute_uri(struct http_server_request *request)
{
    const char *host = strmap_get(request->headers, "host");

    if (host == NULL)
        return NULL;

    return p_strcat(request->pool,
                    "http://",
                    host,
                    request->uri,
                    NULL);
}


/*
 * processor invocation
 *
 */

static void
response_invoke_processor(struct request *request2,
                          http_status_t status, growing_buffer_t response_headers,
                          istream_t body)
{
    struct http_server_request *request = request2->request;
    off_t request_content_length;
    istream_t request_body;
    struct widget *widget;
    unsigned processor_options = 0;

    assert(!request2->response_sent);
    assert(!request2->processed);
    assert(request2->translate.response->process);
    assert(body == NULL || !istream_has_handler(body));

    request2->processed = 1;

    if (body == NULL) {
        response_dispatch(request2, status, response_headers, -1, NULL);
        return;
    }

    async_ref_clear(&request2->url_stream);

    if (http_server_request_has_body(request) && !request2->body_consumed) {
        request_content_length = request->content_length;
        request_body = request->body;
        request2->body_consumed = 1;
    } else {
        request_content_length = 0;
        request_body = NULL;
    }

    processor_env_init(request->pool, &request2->env,
                       request2->http_client_stock,
                       request->remote_host,
                       request_absolute_uri(request),
                       &request2->uri,
                       request2->args,
                       request2->session,
                       request->headers,
                       request_content_length, request_body,
                       embed_widget_callback);

    widget = p_malloc(request->pool, sizeof(*widget));
    widget_init(widget, NULL);
    widget->from_request.session = session_get_widget(request2->env.session,
                                                      strref_dup(request->pool,
                                                                 &request2->uri.base),
                                                      1);

    widget->from_request.proxy_ref = widget_ref_parse(request->pool,
                                                      strmap_get(request2->env.args, "frame"));
    if (widget->from_request.proxy_ref != NULL) {
        request2->env.widget_callback = frame_widget_callback;

        /* do not show the template contents if the browser is
           only interested in one particular widget for
           displaying the frame */
        processor_options |= PROCESSOR_QUIET;
    }

    widget->from_request.focus_ref = widget_ref_parse(request->pool,
                                                      strmap_remove(request2->env.args, "focus"));

    body = processor_new(request->pool, body, widget, &request2->env,
                         processor_options);
    if (widget->from_request.proxy_ref != NULL) {
        /* XXX */
        pool_ref(request->pool);
        widget_proxy_install(&request2->env, request, body);
        request2->response_sent = 1;
        response_close(request2);
        pool_unref(request->pool);
        return;
    }

#ifndef NO_DEFLATE
    if (http_client_accepts_encoding(request->headers, "deflate")) {
        header_write(response_headers, "content-encoding", "deflate");
        body = istream_deflate_new(request->pool, body);
    }
#endif

    assert(!istream_has_handler(body));

    response_dispatch(request2, status, response_headers, -1, body);
}


/*
 * dispatch
 *
 */

void
response_dispatch(struct request *request2,
                  http_status_t status, struct growing_buffer *headers,
                  off_t content_length, istream_t body)
{
    assert(!request2->response_sent);

    if (request2->translate.response->filter != NULL && !request2->filtered) {
        struct http_server_request *request = request2->request;

        request2->filtered = 1;

        pool_ref(request->pool);

        filter_new(request->pool,
                   request2->http_client_stock,
                   request2->translate.response->filter,
                   headers,
                   content_length, body,
                   &response_handler, request2,
                   &request2->filter);
    } else if (request2->translate.response->process && !request2->processed) {
        response_invoke_processor(request2, status, headers, body);
    } else {
        request2->response_sent = 1;
        http_server_response(request2->request,
                             status, headers,
                             content_length, body);
    }
}


/*
 * HTTP response handler
 *
 */

static void 
response_response(http_status_t status, strmap_t headers,
                  off_t content_length, istream_t body,
                  void *ctx)
{
    struct request *request2 = ctx;
    struct http_server_request *request = request2->request;
    pool_t pool = request->pool;
    growing_buffer_t response_headers;

    assert(!request2->response_sent);
    assert(async_ref_defined(&request2->url_stream) ||
           async_ref_defined(&request2->filter));
    assert(body == NULL || !istream_has_handler(body));

    async_ref_clear(&request2->url_stream);
    async_ref_clear(&request2->filter);

    response_headers = growing_buffer_new(request->pool, 2048);
    if (request2->translate.response->process && !request2->processed)
        headers_copy(headers, response_headers, copy_headers_processed);
    else
        headers_copy(headers, response_headers, copy_headers);

    response_dispatch(request2,
                      status, response_headers,
                      content_length, body);

    pool_unref(pool);
}

static void 
response_abort(void *ctx)
{
    struct request *request = ctx;
    pool_t pool = request->request->pool;

    assert(async_ref_defined(&request->url_stream) ||
           async_ref_defined(&request->filter));

    response_close(request);
    pool_unref(pool);
}

const struct http_response_handler response_handler = {
    .response = response_response,
    .abort = response_abort,
};
