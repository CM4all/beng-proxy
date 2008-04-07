/*
 * Query a widget and embed its HTML text after processing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "embed.h"
#include "http-cache.h"
#include "processor.h"
#include "widget.h"
#include "session.h"
#include "cookie.h"
#include "async.h"
#include "google-gadget.h"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

struct embed {
    pool_t pool;

    unsigned num_redirects;

    struct widget *widget;
    struct processor_env *env;
    unsigned options;

    struct http_response_handler_ref handler_ref;
    struct async_operation_ref *async_ref;
};

static const char *const copy_headers[] = {
    "accept",
    "from",
    "cache-control",
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

static const char *
get_env_request_header(const struct processor_env *env, const char *key)
{
    assert(env != NULL);

    if (env->request_headers == NULL)
        return NULL;

    return strmap_get(env->request_headers, key);
}

static void
headers_copy(struct strmap *in, struct strmap *out,
             const char *const* keys)
{
    const char *value;

    for (; *keys != NULL; ++keys) {
        value = strmap_get(in, *keys);
        if (value != NULL)
            strmap_put(out, *keys, value, 1);
    }
}

static struct strmap *
widget_request_headers(struct embed *embed, int with_body)
{
    struct strmap *headers;
    struct widget_session *ws;
    struct session *session;
    const char *p;

    headers = strmap_new(embed->pool, 32);
    strmap_addn(headers, "accept-charset", "utf-8");

    if (embed->env->request_headers != NULL) {
        headers_copy(embed->env->request_headers, headers, copy_headers);
        if (with_body)
            headers_copy(embed->env->request_headers, headers, copy_headers_with_body);
    }

    ws = widget_get_session(embed->widget, 0);
    if (ws != NULL && ws->server != NULL)
        cookie_list_http_header(headers, &ws->server->cookies, embed->pool);

    session = widget_get_session2(embed->widget);
    if (session != NULL && session->language != NULL)
        strmap_addn(headers, "accept-language", session->language);
    else if (embed->env->request_headers != NULL)
        headers_copy(embed->env->request_headers, headers, language_headers);

    if (session != NULL && session->user != NULL)
        strmap_addn(headers, "x-cm4all-beng-user", session->user);

    p = get_env_request_header(embed->env, "user-agent");
    if (p == NULL)
        p = "beng-proxy v" VERSION;
    strmap_addn(headers, "user-agent", p);

    p = get_env_request_header(embed->env, "x-forwarded-for");
    if (p == NULL) {
        if (embed->env->remote_host != NULL)
            strmap_addn(headers, "x-forwarded-for", embed->env->remote_host);
    } else {
        if (embed->env->remote_host == NULL)
            strmap_addn(headers, "x-forwarded-for", p);
        else
            strmap_addn(headers, "x-forwarded-for",
                        p_strcat(embed->pool, p, ", ",
                                 embed->env->remote_host, NULL));
    }

    return headers;
}

static const struct http_response_handler widget_response_handler;

static int
widget_response_redirect(struct embed *embed,
                         strmap_t request_headers, const char *location,
                         istream_t body)
{
    const char *new_uri;
    struct strmap *headers;
    struct strref s;
    const struct strref *p;

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
        /* we have to strdup() the location pointer here because
           istream_close() will invalidate its pool */
        new_uri = location = p_strdup(embed->pool, location);
    else
        location = new_uri;

    strref_set_c(&s, new_uri);

    p = widget_class_relative_uri(embed->widget->class, &s);
    if (p == NULL)
        return 0;

    widget_copy_from_location(embed->widget, p->data, p->length, embed->pool);

    ++embed->num_redirects;

    istream_close(body);
    pool_ref(embed->pool);

    headers = widget_request_headers(embed, 0);

    http_cache_request(embed->env->http_cache,
                       embed->pool,
                       HTTP_METHOD_GET, location, headers, NULL,
                       &widget_response_handler, embed,
                       embed->async_ref);

    return 1;
}

static void 
widget_response_response(http_status_t status, strmap_t headers, istream_t body,
                        void *ctx)
{
    struct embed *embed = ctx;
    const char *location, *cookies, *content_type;

    cookies = strmap_get(headers, "set-cookie2");
    if (cookies == NULL)
        cookies = strmap_get(headers, "set-cookie");
    if (cookies != NULL) {
        struct widget_session *ws = widget_get_session(embed->widget, 1);
        if (ws != NULL && ws->server != NULL)
            cookie_list_set_cookie2(ws->pool, &ws->server->cookies,
                                    cookies);
    }

    if (status >= 300 && status < 400) {
        location = strmap_get(headers, "location");
        if (location != NULL &&
            widget_response_redirect(embed, headers, location, body)) {
            pool_unref(embed->pool);
            return;
        }
    }

    content_type = strmap_get(headers, "content-type");

    switch (embed->widget->display) {
    case WIDGET_DISPLAY_INLINE:
    case WIDGET_DISPLAY_NONE:
    case WIDGET_DISPLAY_IFRAME:
        if (!embed->widget->from_request.raw && body != NULL) {
            if (content_type == NULL ||
                strncmp(content_type, "text/html", 9) != 0) {
                daemon_log(2, "widget sent non-HTML response\n");
                istream_close(body);
                http_response_handler_invoke_abort(&embed->handler_ref);
                pool_unref(embed->pool);
                return;
            }

            if (embed->widget->class->type == WIDGET_TYPE_RAW) {
                http_response_handler_invoke_response(&embed->handler_ref,
                                                      status, headers, body);
                pool_unref(embed->pool);
                return;
            }
                
            processor_new(embed->pool, body,
                          embed->widget, embed->env, embed->options,
                          embed->handler_ref.handler,
                          embed->handler_ref.ctx,
                          embed->async_ref);
            pool_unref(embed->pool);
            return;
        }

        break;

    case WIDGET_DISPLAY_EXTERNAL:
        assert(0);
        break;
    }

    http_response_handler_invoke_response(&embed->handler_ref,
                                          status, headers, body);
    pool_unref(embed->pool);
}

static void
widget_response_abort(void *ctx)
{
    struct embed *embed = ctx;

    http_response_handler_invoke_abort(&embed->handler_ref);
    pool_unref(embed->pool);
}

static const struct http_response_handler widget_response_handler = {
    .response = widget_response_response,
    .abort = widget_response_abort,
};


/*
 * constructor
 *
 */

void
widget_http_request(pool_t pool, struct widget *widget,
                    struct processor_env *env,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    unsigned options = PROCESSOR_REWRITE_URL;
    struct embed *embed;
    struct strmap *headers;

    assert(widget != NULL);
    assert(widget->class != NULL);

    if (widget->class->type == WIDGET_TYPE_GOOGLE_GADGET) {
        /* XXX put this check somewhere else */
        embed_google_gadget(pool, env, widget,
                            handler, handler_ctx, async_ref);
        return;
    }

    assert(widget->display != WIDGET_DISPLAY_EXTERNAL);

    if (widget->class->is_container)
        options |= PROCESSOR_CONTAINER;

    embed = p_malloc(pool, sizeof(*embed));
    embed->pool = pool;
    embed->num_redirects = 0;
    embed->widget = widget;
    embed->env = env;
    embed->options = options;

    headers = widget_request_headers(embed, widget->from_request.body != NULL);

    pool_ref(embed->pool);

    http_response_handler_set(&embed->handler_ref, handler, handler_ctx);
    embed->async_ref = async_ref;

    http_cache_request(env->http_cache,
                       pool,
                       widget->from_request.method,
                       widget_real_uri(pool, widget),
                       headers,
                       widget->from_request.body,
                       &widget_response_handler, embed, async_ref);
}
