/*
 * Embed a processed HTML document
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "embed.h"
#include "url-stream.h"
#include "processor.h"
#include "widget.h"
#include "header-writer.h"
#include "session.h"
#include "cookie.h"
#include "http-client.h"

#include <assert.h>
#include <string.h>

struct embed {
    pool_t pool;

    unsigned num_redirects;

    struct widget *widget;
    const struct processor_env *env;
    unsigned options;

    url_stream_t url_stream;

    istream_t delayed;
};

static void
embed_delayed_abort(void *ctx)
{
    struct embed *embed = (struct embed *)ctx;

    if (embed->url_stream == NULL)
        return;

    embed->delayed = NULL;

    url_stream_close(embed->url_stream);
    assert(embed->url_stream == NULL);
}

static const struct http_client_response_handler embed_response_handler;

static void 
embed_response_response(http_status_t status, strmap_t headers,
                        off_t content_length, istream_t body,
                        void *ctx)
{
    struct embed *embed = ctx;
    const char *location, *cookies, *content_type;
    istream_t input = body, delayed;

    (void)status;

    assert(embed->url_stream != NULL);
    embed->url_stream = NULL;

    location = strmap_get(headers, "location");
    if (location != NULL && embed->num_redirects < 8) {
        const char *new_uri;

        new_uri = widget_absolute_uri(embed->pool, embed->widget,
                                      location, strlen(location));
        if (new_uri == NULL)
            new_uri = p_strdup(embed->pool, location);

        location = new_uri;

        new_uri = widget_class_relative_uri(embed->widget->class, new_uri);
        if (new_uri != NULL) {
            embed->widget->from_request.path_info = new_uri;

            ++embed->num_redirects;

            delayed = embed->delayed;
            embed->delayed = NULL;

            istream_close(input);

            embed->delayed = delayed;
            pool_ref(embed->pool);

            widget_determine_real_uri(embed->pool, embed->env, embed->widget);

            embed->url_stream = url_stream_new(embed->pool,
                                               HTTP_METHOD_GET, location, /*XXX headers*/ NULL,
                                               0, NULL,
                                               &embed_response_handler, embed);
            if (embed->url_stream == NULL) {
                istream_delayed_set(embed->delayed,
                                    istream_string_new(embed->pool, "Failed to create url_stream object."));
                pool_unref(embed->pool);
            }

            return;
        }
    }

    content_type = strmap_get(headers, "content-type");
    if (content_type != NULL && strncmp(content_type, "text/html", 9) == 0) {
        /* HTML resources must be processed */
        input = processor_new(istream_pool(embed->delayed), input,
                              embed->widget, embed->env, embed->options);
        content_length = -1;
    }

    cookies = strmap_get(headers, "set-cookie2");
    if (cookies == NULL)
        cookies = strmap_get(headers, "set-cookie");
    if (cookies != NULL) {
        struct widget_session *ws = widget_get_session(embed->widget, 1);
        if (ws != NULL)
            cookie_list_set_cookie2(ws->pool, &ws->cookies,
                                    cookies);
    }

    pool_ref(embed->pool);

    if (embed->widget->from_request.proxy && embed->env->proxy_callback != NULL) {
        /* this is the request for IFRAME contents - send it directly
           to to http_server object, including headers */

        embed->env->proxy_callback(HTTP_STATUS_OK, headers,
                                   content_length, input,
                                   embed->env->proxy_callback_ctx);
        pool_unref(embed->pool);
        return;
    }

    delayed = embed->delayed;
    embed->delayed = NULL;

    if (input == body && !embed->widget->from_request.proxy &&
        embed->widget->id != NULL) {
        /* it cannot be inserted into the HTML stream, so put it into
           an iframe */

        if (embed->widget->display == WIDGET_DISPLAY_INLINE) {
            if (content_type != NULL && strncmp(content_type, "image/", 6) == 0)
                embed->widget->display = WIDGET_DISPLAY_IMG;
            else
                embed->widget->display = WIDGET_DISPLAY_IFRAME;
        }

        istream_close(input);

        input = embed->env->widget_callback(embed->pool,
                                            embed->env, embed->widget);
    }

    if (input == body) {
        /* still no sucess in framing this strange resource - insert
           an error message instead of widget contents */

        istream_close(input);
        input = istream_string_new(istream_pool(embed->delayed),
                                   "Not an HTML document");
    }

    istream_delayed_set(delayed, input);

    pool_unref(embed->pool);
}

static void 
embed_response_free(void *ctx)
{
    struct embed *embed = ctx;

    embed->url_stream = NULL;

    if (embed->delayed != NULL)
        istream_free(&embed->delayed);

    pool_unref(embed->pool);
}

static const struct http_client_response_handler embed_response_handler = {
    .response = embed_response_response,
    .free = embed_response_free,
};


/*
 * constructor
 *
 */

istream_t
embed_new(pool_t pool, http_method_t method, const char *url,
          off_t request_content_length,
          istream_t request_body,
          struct widget *widget,
          const struct processor_env *env,
          unsigned options)
{
    struct embed *embed;
    growing_buffer_t headers;
    struct widget_session *ws;
    static const char *const copy_headers[] = {
        "accept",
        "accept-language",
        "from",
        NULL,
    };
    static const char *const copy_headers_with_body[] = {
        "content-encoding",
        "content-language",
        "content-md5",
        "content-range",
        "content-type",
        NULL,
    };

    assert(url != NULL);

    headers = growing_buffer_new(pool, 1024);
    header_write(headers, "accept-charset", "utf-8");
    header_write(headers, "connection", "close");
    if (env->request_headers != NULL) {
        headers_copy(env->request_headers, headers, copy_headers);
        if (request_body != NULL)
            headers_copy(env->request_headers, headers, copy_headers_with_body);
    }

    ws = widget_get_session(widget, 0);
    if (ws != NULL)
        cookie_list_http_header(headers, &ws->cookies);

    embed = p_malloc(pool, sizeof(*embed));
    embed->pool = pool;
    embed->num_redirects = 0;
    embed->widget = widget;
    embed->env = env;
    embed->options = options;
    embed->delayed = istream_delayed_new(pool, embed_delayed_abort, embed);

    embed->url_stream = url_stream_new(pool,
                                       method, url, headers,
                                       request_content_length,
                                       request_body,
                                       &embed_response_handler, embed);
    if (embed->url_stream == NULL)
        istream_delayed_set(embed->delayed,
                            istream_string_new(pool, "Failed to create url_stream object."));
    else
        pool_ref(embed->pool);

    return embed->delayed;
}
