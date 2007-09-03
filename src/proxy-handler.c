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
#include "args.h"

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
        struct processor_env *env = &pt->env;

        memset(env, 0, sizeof(*env));
        env->external_uri = &pt->translated->uri;

        if (pt->translated->uri.args != NULL)
            env->args = args_parse(pt->request->pool, pt->translated->uri.args,
                                   pt->translated->uri.args_length);

        pool_ref(pt->request->pool);

        body = processor_new(pt->request->pool, body, NULL, &pt->env);

        pool_unref(pt->request->pool);
        if (body == NULL) {
            /* XXX */
            abort();
        }

        header_write(response_headers, "content-type", "text/html");
        content_length = (off_t)-1;
    }

    assert(body->handler == NULL);

    http_server_response(pt->request, HTTP_STATUS_OK,
                         response_headers,
                         content_length, body);

    proxy_transfer_close(pt);
}

static const char *const copy_headers[] = {
    "user-agent",
    NULL
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
        http_server_send_message(request,
                                 HTTP_STATUS_METHOD_NOT_ALLOWED,
                                 "This method is not supported.");
        return;
    }

    pt = p_calloc(request->pool, sizeof(*pt));
    pt->pool = request->pool;
    pt->request = request;
    pt->translated = translated;

    pt->url_stream = url_stream_new(request->pool,
                                    HTTP_METHOD_GET, translated->path, NULL,
                                    proxy_http_client_callback, pt);
    if (pt->url_stream == NULL) {
        http_server_send_message(request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        return;
    }

    pool_ref(pt->pool);
}
