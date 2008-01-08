/*
 * Query a widget and embed its HTML text after processing.
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
#include "async.h"

#include <assert.h>
#include <string.h>

struct embed {
    pool_t pool;

    unsigned num_redirects;

    struct widget *widget;
    struct processor_env *env;
    unsigned options;

    struct async_operation_ref url_stream;

    istream_t delayed;
};

static const char *const copy_headers[] = {
    "accept",
    "from",
    NULL,
};

static const char *const language_headers[] = {
    "accept-language",
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

static growing_buffer_t
embed_request_headers(struct embed *embed, int with_body)
{
    growing_buffer_t headers;
    struct widget_session *ws;
    struct session *session;
    const char *p;

    headers = growing_buffer_new(embed->pool, 1024);
    header_write(headers, "accept-charset", "utf-8");

    if (embed->env->request_headers != NULL) {
        headers_copy(embed->env->request_headers, headers, copy_headers);
        if (with_body)
            headers_copy(embed->env->request_headers, headers, copy_headers_with_body);
    }

    ws = widget_get_session(embed->widget, 0);
    if (ws != NULL)
        cookie_list_http_header(headers, &ws->cookies);

    session = widget_get_session2(embed->widget);
    if (session != NULL && session->language != NULL)
        header_write(headers, "accept-language", session->language);
    else if (embed->env->request_headers != NULL)
        headers_copy(embed->env->request_headers, headers, language_headers);

    if (session != NULL && session->user != NULL)
        header_write(headers, "x-cm4all-beng-user", session->user);

    if (embed->env->request_headers == NULL)
        p = NULL;
    else
        p = strmap_get(embed->env->request_headers, "user-agent");
    if (p == NULL)
        p = "beng-proxy v" VERSION;
    header_write(headers, "user-agent", p);

    p = strmap_get(embed->env->request_headers, "x-forwarded-for");
    if (p == NULL) {
        if (embed->env->remote_host != NULL)
            header_write(headers, "x-forwarded-for", embed->env->remote_host);
    } else {
        if (embed->env->remote_host == NULL)
            header_write(headers, "x-forwarded-for", p);
        else
            header_write(headers, "x-forwarded-for",
                         p_strcat(embed->pool, p, ", ",
                                  embed->env->remote_host, NULL));
    }

    return headers;
}

static void
embed_delayed_abort(void *ctx)
{
    struct embed *embed = (struct embed *)ctx;

    if (!async_ref_defined(&embed->url_stream))
        return;

    embed->delayed = NULL;

    async_abort(&embed->url_stream);
}

static const struct http_response_handler embed_response_handler;

static int
embed_redirect(struct embed *embed,
               strmap_t request_headers, const char *location,
               istream_t body)
{
    const char *new_uri;
    istream_t delayed;
    growing_buffer_t headers;

    if (embed->num_redirects >= 8)
        return 0;

    if (strncmp(location, ";translate=", 11) == 0) {
        /* XXX this special URL syntax should be redesigned */
        location = widget_translation_uri(embed->pool, embed->env->external_uri,
                                          embed->env->args, location + 11);
        strmap_put(request_headers, "location", location, 1);
        return 0;
    }

    new_uri = widget_absolute_uri(embed->pool, embed->widget,
                                  location, strlen(location));
    if (new_uri == NULL)
        new_uri = p_strdup(embed->pool, location);

    location = new_uri;

    new_uri = widget_class_relative_uri(embed->widget->class, new_uri);
    if (new_uri == NULL)
        return 0;

    widget_copy_from_location(embed->widget, new_uri, embed->pool);
    widget_determine_real_uri(embed->pool, embed->widget);

    ++embed->num_redirects;

    delayed = embed->delayed;
    embed->delayed = NULL;

    istream_close(body);

    embed->delayed = delayed;
    pool_ref(embed->pool);

    headers = embed_request_headers(embed, 0);

    url_stream_new(embed->pool,
                   embed->env->http_client_stock,
                   HTTP_METHOD_GET, location, headers, NULL,
                   &embed_response_handler, embed,
                   &embed->url_stream);

    return 1;
}

static void 
embed_response_response(http_status_t status, strmap_t headers, istream_t body,
                        void *ctx)
{
    struct embed *embed = ctx;
    const char *location, *cookies, *content_type;
    istream_t input = body, delayed;

    (void)status;

    assert(async_ref_defined(&embed->url_stream));
    async_ref_clear(&embed->url_stream);

    cookies = strmap_get(headers, "set-cookie2");
    if (cookies == NULL)
        cookies = strmap_get(headers, "set-cookie");
    if (cookies != NULL) {
        struct widget_session *ws = widget_get_session(embed->widget, 1);
        if (ws != NULL)
            cookie_list_set_cookie2(ws->pool, &ws->cookies,
                                    cookies);
    }

    if (status >= 300 && status < 400) {
        location = strmap_get(headers, "location");
        if (location != NULL && embed_redirect(embed, headers, location, body)) {
            pool_unref(embed->pool);
            return;
        }
    }

    content_type = strmap_get(headers, "content-type");
    if (content_type != NULL && strncmp(content_type, "text/html", 9) == 0)
        /* HTML resources must be processed */
        input = processor_new(istream_pool(embed->delayed), input,
                              embed->widget, embed->env, embed->options);

    if (embed->widget->from_request.proxy &&
        http_response_handler_defined(&embed->env->response_handler)) {
        /* this is the request for IFRAME contents - send it directly
           to the http_server object, including headers */

        http_response_handler_invoke_response(&embed->env->response_handler,
                                              status, headers, input);
        pool_unref(embed->pool);
        return;
    }

    delayed = embed->delayed;
    embed->delayed = NULL;

    if (input == body && embed->widget->id != NULL) {
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
        input = istream_string_new(istream_pool(delayed),
                                   "Not an HTML document");
    }

    istream_delayed_set(delayed, input);

    pool_unref(embed->pool);
}

static void 
embed_response_abort(void *ctx)
{
    struct embed *embed = ctx;

    async_ref_clear(&embed->url_stream);

    if (embed->delayed != NULL)
        istream_free(&embed->delayed);

    pool_unref(embed->pool);
}

static const struct http_response_handler embed_response_handler = {
    .response = embed_response_response,
    .abort = embed_response_abort,
};


/*
 * constructor
 *
 */

istream_t
embed_new(pool_t pool, http_method_t method, const char *url,
          istream_t request_body,
          struct widget *widget,
          struct processor_env *env,
          unsigned options)
{
    struct embed *embed;
    growing_buffer_t headers;

    assert(url != NULL);

    embed = p_malloc(pool, sizeof(*embed));
    embed->pool = pool;
    embed->num_redirects = 0;
    embed->widget = widget;
    embed->env = env;
    embed->options = options;
    embed->delayed = istream_delayed_new(pool, embed_delayed_abort, embed);

    headers = embed_request_headers(embed, request_body != NULL);

    pool_ref(embed->pool);

    url_stream_new(pool,
                   env->http_client_stock,
                   method, url, headers,
                   request_body,
                   &embed_response_handler, embed,
                   &embed->url_stream);

    /* XXX has the embed_response_handler already been called? */
    return embed->delayed;
}
