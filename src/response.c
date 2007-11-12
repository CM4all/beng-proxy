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

    if (request->url_stream != NULL) {
        url_stream_t url_stream = request->url_stream;
        request->url_stream = NULL;
        url_stream_close(url_stream);
    }

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
    struct widget *widget;
    unsigned processor_options = 0;
    pool_t pool;

    assert(!request2->response_sent);
    assert(!request2->processed);
    assert(request2->translate.response->process);
    assert(!istream_has_handler(body));

    pool = request->pool;

    request2->processed = 1;

    request2->url_stream = NULL;

    processor_env_init(request->pool, &request2->env,
                       request->remote_host,
                       request_absolute_uri(request),
                       &request2->uri,
                       request2->args,
                       request2->session,
                       request->headers,
                       request->content_length, request->body,
                       embed_widget_callback);
    if (request2->env.frame != NULL) { /* XXX */
        request2->env.widget_callback = frame_widget_callback;

        /* do not show the template contents if the browser is
           only interested in one particular widget for
           displaying the frame */
        processor_options |= PROCESSOR_QUIET;
    }

    widget = p_malloc(request->pool, sizeof(*widget));
    widget_init(widget, NULL);
    widget->from_request.session = session_get_widget(request2->env.session,
                                                      p_strndup(request->pool,
                                                                request2->uri.base,
                                                                request2->uri.base_length),
                                                      1);

    body = processor_new(request->pool, body, widget, &request2->env,
                             processor_options);
    if (request2->env.frame != NULL) {
        /* XXX */
        pool_ref(pool);
        widget_proxy_install(&request2->env, request, body);
        request2->response_sent = 1;
        response_close(request2);
        pool_unref(pool);
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

        request2->filter = filter_new(request->pool,
                                      request2->translate.response->filter,
                                      headers,
                                      content_length, body,
                                      &response_handler, request2);
        if (request2->filter == NULL) {
            if (body != NULL)
                istream_close(body);
            if (request->body != NULL)
                istream_close(request->body);

            http_server_send_message(request,
                                     HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                     "Internal server error");
        }
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
    assert(request2->url_stream != NULL || request2->filter != NULL);
    assert(!istream_has_handler(body));

    request2->url_stream = NULL;
    request2->filter = NULL;

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

    assert(request->url_stream != NULL || request->filter != NULL);

    response_close(request);
    pool_unref(pool);
}

const struct http_response_handler response_handler = {
    .response = response_response,
    .abort = response_abort,
};
