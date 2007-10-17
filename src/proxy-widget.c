/*
 * Handle proxying of widget contents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "proxy-widget.h"
#include "http-util.h"
#include "header-writer.h"

#include <assert.h>

struct widget_proxy {
    struct http_server_request *request;
    istream_t body;
};


/*
 * istream handler
 *
 */

static size_t
proxy_source_data(const void *data, size_t length, void *ctx)
{
    struct widget_proxy *wp = ctx;
    
    (void)data;
    (void)length;

    assert(wp->body != NULL);

    /* the processor must not send data. */

    istream_free_unref_handler(&wp->body);

    if (wp->request != NULL) {
        http_server_send_message(wp->request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "The processor sent data in proxy-widget mode");
        wp->request = NULL;
    }

    return 0;
}

static void
proxy_source_eof(void *ctx)
{
    struct widget_proxy *wp = ctx;

    assert(wp->body != NULL);

    /* the processor must invoke widget_proxy_callback() before it
       reports EOF */

    istream_clear_unref_handler(&wp->body);

    if (wp->request != NULL) {
        http_server_send_message(wp->request, HTTP_STATUS_NOT_FOUND,
                                 "The requested widget was not found");
        wp->request = NULL;
    }
}

static void
proxy_source_free(void *ctx)
{
    struct widget_proxy *wp = ctx;

    assert(wp->body != NULL);

    /* processor error */

    istream_clear_unref(&wp->body);

    if (wp->request != NULL) {
        http_server_send_message(wp->request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Error in processor istream");
        wp->request = NULL;
    }
}

static const struct istream_handler proxy_body_handler = {
    .data = proxy_source_data,
    .eof = proxy_source_eof,
    .free = proxy_source_free,
};


/*
 * processor_env.proxy_callback
 *
 */

static void
widget_proxy_callback(http_status_t status,
                      strmap_t headers,
                      off_t content_length, istream_t body,
                      void *ctx)
{
    struct widget_proxy *wp = ctx;
    growing_buffer_t headers2;
    static const char *const copy_headers[] = {
        "age",
        "etag",
        "content-encoding",
        "content-type",
        "retry-after",
        "vary",
        NULL,
    };

    headers2 = growing_buffer_new(wp->request->pool, 2048);
    headers_copy(headers, headers2, copy_headers);

    assert(wp->body != NULL);
    assert(wp->request != NULL);

    istream_free_unref_handler(&wp->body);

#ifndef NO_DEFLATE
    if (content_length == (off_t)-1 &&
        strmap_get(headers, "content-encoding") == NULL &&
        http_client_accepts_encoding(wp->request->headers, "deflate")) {
        header_write(headers2, "content-encoding", "deflate");
        body = istream_deflate_new(wp->request->pool, body);
    }
#endif

    http_server_response(wp->request, status, headers2, content_length, body);
    wp->request = NULL;
}


/*
 * constructor
 *
 */

void
widget_proxy_install(struct processor_env *env,
                     struct http_server_request *request,
                     istream_t body)
{
    struct widget_proxy *wp = p_malloc(request->pool, sizeof(*wp));

    assert(env->frame != NULL);

    wp->request = request;
    istream_assign_ref_handler(&wp->body, body, &proxy_body_handler,
                               wp, 0);

    env->proxy_callback = widget_proxy_callback;
    env->proxy_callback_ctx = wp;

    istream_read(wp->body);
}
