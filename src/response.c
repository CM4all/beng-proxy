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
    pool_t pool;

    assert(request != NULL);
    assert(request->request != NULL);
    assert(request->request->pool != NULL);

    pool = request->request->pool;

    if (request->url_stream != NULL) {
        url_stream_t url_stream = request->url_stream;
        request->url_stream = NULL;
        url_stream_close(url_stream);
    }

    if (!request->response_sent)
        http_server_send_message(request->request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");

    pool_unref(pool);
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
    growing_buffer_t response_headers;

    assert(!request2->response_sent);

    response_headers = growing_buffer_new(request->pool, 2048);

    if (request2->translate.response->process) {
        struct widget *widget;
        unsigned processor_options = 0;

        /* XXX request body? */
        processor_env_init(request->pool, &request2->env,
                           request->remote_host,
                           request_absolute_uri(request),
                           &request2->uri,
                           request2->args,
                           request2->session,
                           request->headers,
                           0, NULL,
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
        widget->from_request.session = session_get_widget(request2->env.session, request2->uri.base, 1);

        pool_ref(request->pool);

        body = processor_new(request->pool, body, widget, &request2->env,
                             processor_options);
        if (request2->env.frame != NULL) {
            /* XXX */
            widget_proxy_install(&request2->env, request, body);
            pool_unref(request->pool);
            response_close(request2);
            return;
        }

#ifndef NO_DEFLATE
        if (http_client_accepts_encoding(request->headers, "deflate")) {
            header_write(response_headers, "content-encoding", "deflate");
            body = istream_deflate_new(request->pool, body);
        }
#endif

        pool_unref(request->pool);

        content_length = (off_t)-1;

        headers_copy(headers, response_headers, copy_headers_processed);
    } else {
        headers_copy(headers, response_headers, copy_headers);
    }

    assert(!istream_has_handler(body));

    request2->response_sent = 1;
    http_server_response(request, status,
                         response_headers,
                         content_length, body);
}

static void 
response_free(void *ctx)
{
    struct request *request = ctx;

    request->url_stream = NULL;

    response_close(request);
}

const struct http_response_handler response_handler = {
    .response = response_response,
    .free = response_free,
};
