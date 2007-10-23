/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "connection.h"
#include "handler.h"
#include "url-stream.h"
#include "processor.h"
#include "header-writer.h"
#include "widget.h"
#include "embed.h"
#include "frame.h"
#include "http-util.h"
#include "proxy-widget.h"
#include "session.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct proxy_transfer {
    pool_t pool;
    struct http_server_request *request;
    struct translated *translated;
    url_stream_t url_stream;
    struct processor_env env;
};

static void
proxy_transfer_close(struct proxy_transfer *pt)
{
    pool_t pool = pt->pool;

    assert(pt->pool != NULL);

    if (pt->url_stream != NULL) {
        url_stream_t url_stream = pt->url_stream;
        pt->url_stream = NULL;
        url_stream_close(url_stream);
    }

    pt->request = NULL;
    pt->pool = NULL;
    pool_unref(pool);
}

static void 
proxy_http_client_callback(http_status_t status, strmap_t headers,
                           off_t content_length, istream_t body,
                           void *ctx)
{
    struct proxy_transfer *pt = ctx;
    const char *value;
    growing_buffer_t response_headers;

    assert(pt->url_stream != NULL);
    pt->url_stream = NULL;

    if (status == 0) {
        /* XXX */
        proxy_transfer_close(pt);
        return;
    }

    response_headers = growing_buffer_new(pt->request->pool, 2048);
    /* XXX copy headers */

    value = strmap_get(headers, "content-type");
    if (value != NULL && strncmp(value, "text/html", 9) == 0) {
        struct widget *widget;
        unsigned processor_options = 0;

        /* XXX request body? */
        processor_env_init(pt->request->pool, &pt->env, &pt->translated->uri,
                           pt->request->headers,
                           0, NULL,
                           embed_widget_callback);
        if (pt->env.frame != NULL) { /* XXX */
            pt->env.widget_callback = frame_widget_callback;

            /* do not show the template contents if the browser is
               only interested in one particular widget for
               displaying the frame */
            processor_options |= PROCESSOR_QUIET;
        }

        widget = p_malloc(pt->request->pool, sizeof(*widget));
        widget_init(widget, NULL);
        widget->from_request.session = session_get_widget(pt->env.session, pt->translated->uri.base, 1);

        pool_ref(pt->request->pool);

        body = processor_new(pt->request->pool, body, widget, &pt->env,
                             processor_options);
        if (pt->env.frame != NULL) {
            /* XXX */
            widget_proxy_install(&pt->env, pt->request, body);
            pool_unref(pt->request->pool);
            proxy_transfer_close(pt);
            return;
        }

#ifndef NO_DEFLATE
        if (http_client_accepts_encoding(pt->request->headers, "deflate")) {
            header_write(response_headers, "content-encoding", "deflate");
            body = istream_deflate_new(pt->request->pool, body);
        }
#endif

        pool_unref(pt->request->pool);

        header_write(response_headers, "content-type", "text/html");
        content_length = (off_t)-1;
    }

    assert(!istream_has_handler(body));

    http_server_response(pt->request, HTTP_STATUS_OK,
                         response_headers,
                         content_length, body);

    proxy_transfer_close(pt);
}

/*
static const char *const copy_headers[] = {
    "user-agent",
    NULL
};
*/

void
proxy_callback(struct client_connection *connection,
               struct http_server_request *request,
               struct translated *translated)
{
    struct proxy_transfer *pt;
    istream_t body;

    (void)connection;

    pt = p_calloc(request->pool, sizeof(*pt));
    pt->pool = request->pool;
    pt->request = request;
    pt->translated = translated;

    if (request->body == NULL)
        body = NULL;
    else
        body = istream_hold_new(request->pool, request->body);

    pt->url_stream = url_stream_new(request->pool,
                                    request->method, translated->path, NULL,
                                    request->content_length, body,
                                    proxy_http_client_callback, pt);
    if (pt->url_stream == NULL) {
        http_server_send_message(request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        return;
    }

    pool_ref(pt->pool);
}
