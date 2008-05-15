/*
 * Send HTTP requests to a widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-http.h"
#include "http-cache.h"
#include "processor.h"
#include "widget.h"
#include "session.h"
#include "cookie-client.h"
#include "async.h"
#include "http-util.h"
#include "uri-address.h"
#include "strref2.h"
#include "strref-pool.h"
#include "dpool.h"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

struct embed {
    pool_t pool;

    unsigned num_redirects;

    struct widget *widget;
    struct processor_env *env;
    const char *host_and_port;

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
uri_host_and_port(pool_t pool, const char *uri)
{
    const char *slash;

    if (memcmp(uri, "http://", 7) != 0)
        return NULL;

    uri += 7;
    slash = strchr(uri, '/');
    if (slash == NULL)
        return uri;

    return p_strndup(pool, uri, slash - uri);
}

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

static const char *
uri_path(const char *uri)
{
    const char *p = strchr(uri, ':');
    if (p == NULL || p[1] != '/')
        return uri;
    if (p[2] != '/')
        return p + 1;
    p = strchr(p + 3, '/');
    if (p == NULL)
        return "";
    return p;
}

static struct strmap *
widget_request_headers(struct embed *embed, int with_body)
{
    struct strmap *headers;
    struct session *session;
    const char *p;

    headers = strmap_new(embed->pool, 32);
    strmap_addn(headers, "accept-charset", "utf-8");

    if (embed->env->request_headers != NULL) {
        headers_copy(embed->env->request_headers, headers, copy_headers);
        if (with_body)
            headers_copy(embed->env->request_headers, headers, copy_headers_with_body);
    }

    session = embed->env->session;

    if (embed->host_and_port != NULL && session != NULL) {
        const char *path = uri_path(widget_real_uri(embed->pool,
                                                    embed->widget));

        lock_lock(&session->lock);
        cookie_jar_http_header(session->cookies, embed->host_and_port, path,
                               headers, embed->pool);
        lock_unlock(&session->lock);
    }

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

static bool
widget_response_redirect(struct embed *embed, const char *location,
                         istream_t body)
{
    const char *new_uri;
    struct uri_with_address *uwa;
    struct strmap *headers;
    struct strref s;
    const struct strref *p;

    if (embed->num_redirects >= 8)
        return false;

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
        return false;

    widget_copy_from_location(embed->widget, embed->env->session,
                              p->data, p->length, embed->pool);

    ++embed->num_redirects;

    istream_close(body);
    pool_ref(embed->pool);

    uwa = uri_address_dup(embed->pool, embed->widget->class->address);
    uwa->uri = location;

    headers = widget_request_headers(embed, 0);

    http_cache_request(embed->env->http_cache,
                       embed->pool,
                       HTTP_METHOD_GET, uwa, headers, NULL,
                       &widget_response_handler, embed,
                       embed->async_ref);

    return true;
}

/**
 * The widget response is going to be embedded into a template; check
 * its content type and run the processor (if applicable).
 */
static void
widget_response_process(struct embed *embed, http_status_t status,
                        strmap_t headers, istream_t body)
{
    const char *content_type;
    struct strref *charset, charset_buffer;
    unsigned options;

    content_type = strmap_get(headers, "content-type");

    if (content_type == NULL ||
        strncmp(content_type, "text/html", 9) != 0) {
        daemon_log(2, "widget sent non-HTML response\n");
        istream_close(body);
        http_response_handler_invoke_abort(&embed->handler_ref);
        return;
    }

    charset = http_header_param(&charset_buffer, content_type, "charset");
    if (charset != NULL && strref_lower_cmp_literal(charset, "utf-8") != 0 &&
        strref_lower_cmp_literal(charset, "utf8") != 0) {
        /* beng-proxy expects all widgets to send their HTML code in
           utf-8; this widget however used a different charset.
           Automatically convert it with istream_iconv */
        const char *charset2 = strref_dup(embed->pool, charset);
        istream_t ic = istream_iconv_new(embed->pool, body, "utf-8", charset2);
        if (ic == NULL) {
            daemon_log(2, "widget sent unknown charset '%s'\n", charset2);
            istream_close(body);
            http_response_handler_invoke_abort(&embed->handler_ref);
            return;
        }

        daemon_log(6, "charset conversion '%s' -> utf-8\n", charset2);
        body = ic;

        headers = strmap_dup(embed->pool, headers);
        strmap_put(headers, "content-type", "text/html; charset=utf-8", 1);
    }

    if (embed->widget->class->type == WIDGET_TYPE_RAW) {
        http_response_handler_invoke_response(&embed->handler_ref,
                                              status, headers, body);
        return;
    }

    options = PROCESSOR_REWRITE_URL;
    if (embed->widget->class->is_container)
        options |= PROCESSOR_CONTAINER;

    processor_new(embed->pool, body,
                  embed->widget, embed->env, options,
                  embed->handler_ref.handler,
                  embed->handler_ref.ctx,
                  embed->async_ref);
}

static void 
widget_response_response(http_status_t status, strmap_t headers, istream_t body,
                         void *ctx)
{
    struct embed *embed = ctx;
    const char *translate;

    if (embed->host_and_port != NULL) {
        const char *cookies = strmap_get(headers, "set-cookie2");
        if (cookies == NULL)
            cookies = strmap_get(headers, "set-cookie");
        if (cookies != NULL) {
            struct session *session = embed->env->session;
            if (session != NULL) {
                lock_lock(&session->lock);
                cookie_jar_set_cookie2(session->cookies, cookies,
                                       embed->host_and_port);
                lock_unlock(&session->lock);
            }
        }
    }

    translate = strmap_get(headers, "x-cm4all-beng-translate");
    if (translate != NULL) {
        struct session *session = embed->env->session;
        if (session != NULL)
            session->translate = d_strdup(session->pool, translate);
    }

    if (status >= 300 && status < 400) {
        const char *location = strmap_get(headers, "location");
        if (location != NULL &&
            widget_response_redirect(embed, location, body)) {
            pool_unref(embed->pool);
            return;
        }
    }

    if (embed->widget->from_request.raw || body == NULL)
        http_response_handler_invoke_response(&embed->handler_ref,
                                              status, headers, body);
    else
        widget_response_process(embed, status, headers, body);

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
    struct embed *embed;
    struct uri_with_address *uwa;
    struct strmap *headers;

    assert(widget != NULL);
    assert(widget->class != NULL);

    embed = p_malloc(pool, sizeof(*embed));
    embed->pool = pool;

    embed->num_redirects = 0;
    embed->widget = widget;
    embed->env = env;
    embed->host_and_port =
        uri_host_and_port(pool, embed->widget->class->address->uri);

    uwa = uri_address_dup(pool, widget->class->address);
    uwa->uri = widget_real_uri(pool, widget);

    headers = widget_request_headers(embed, widget->from_request.body != NULL);

    pool_ref(embed->pool);

    http_response_handler_set(&embed->handler_ref, handler, handler_ctx);
    embed->async_ref = async_ref;

    http_cache_request(env->http_cache,
                       pool,
                       widget->from_request.method,
                       uwa,
                       headers,
                       widget->from_request.body,
                       &widget_response_handler, embed, async_ref);
}
