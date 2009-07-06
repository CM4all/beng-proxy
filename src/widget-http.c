/*
 * Send HTTP requests to a widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-http.h"
#include "processor.h"
#include "widget.h"
#include "session.h"
#include "cookie-client.h"
#include "async.h"
#include "dpool.h"
#include "get.h"
#include "fcache.h"
#include "header-writer.h"
#include "transformation.h"
#include "global.h"
#include "resource-tag.h"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

struct embed {
    pool_t pool;

    unsigned num_redirects;

    struct widget *widget;
    struct processor_env *env;
    const char *host_and_port;

    /**
     * the next transformation to be applied to the widget response
     */
    const struct transformation *transformation;

    /**
     * An identifier for the source stream of the current
     * transformation.  This is used by the filter cache to address
     * resources.
     */
    const char *resource_tag;

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
    "content-disposition",
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

    return strmap_get_checked(env->request_headers, key);
}

static void
headers_copy2(struct strmap *in, struct strmap *out,
              const char *const* keys)
{
    const char *value;

    for (; *keys != NULL; ++keys) {
        value = strmap_get(in, *keys);
        if (value != NULL)
            strmap_set(out, *keys, value);
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
widget_request_headers(struct embed *embed, bool with_body)
{
    struct strmap *headers;
    struct session *session;
    const char *p;

    headers = strmap_new(embed->pool, 32);
    strmap_add(headers, "accept-charset", "utf-8");

    if (embed->env->request_headers != NULL) {
        headers_copy2(embed->env->request_headers, headers, copy_headers);
        if (with_body)
            headers_copy2(embed->env->request_headers, headers, copy_headers_with_body);
    }

    session = session_get(embed->env->session_id);

    if (embed->host_and_port != NULL && session != NULL) {
        const char *path = uri_path(widget_address(embed->widget)->u.http->uri);

        cookie_jar_http_header(session->cookies, embed->host_and_port, path,
                               headers, embed->pool);
    }

    if (session != NULL && session->language != NULL)
        strmap_add(headers, "accept-language",
                   p_strdup(embed->pool, session->language));
    else if (embed->env->request_headers != NULL)
        headers_copy2(embed->env->request_headers, headers, language_headers);

    if (session != NULL && session->user != NULL)
        strmap_add(headers, "x-cm4all-beng-user",
                   p_strdup(embed->pool, session->user));

    session_put(session);

    p = get_env_request_header(embed->env, "user-agent");
    if (p == NULL)
        p = "beng-proxy v" VERSION;
    strmap_add(headers, "user-agent", p);

    p = get_env_request_header(embed->env, "via");
    if (p == NULL) {
        if (embed->env->remote_host != NULL)
            strmap_add(headers, "via",
                       p_strcat(embed->pool, "1.1 ",
                                embed->env->remote_host, NULL));
    } else {
        if (embed->env->remote_host == NULL)
            strmap_add(headers, "via", p);
        else
            strmap_add(headers, "via",
                       p_strcat(embed->pool, p, ", 1.1 ",
                                embed->env->remote_host, NULL));
    }

    if (embed->widget->headers != NULL) {
        /* copy HTTP request headers from template */
        const struct strmap_pair *pair;

        strmap_rewind(embed->widget->headers);

        while ((pair = strmap_next(embed->widget->headers)) != NULL)
            strmap_add(headers,
                       p_strdup(embed->pool, pair->key),
                       p_strdup(embed->pool, pair->value));
    }

    return headers;
}

static const struct http_response_handler widget_response_handler;

static bool
widget_response_redirect(struct embed *embed, const char *location,
                         istream_t body)
{
    const struct resource_address *address;
    struct session *session;
    struct strmap *headers;
    struct strref strref_buffer;
    const struct strref *p;
    struct resource_address address_buffer;

    if (embed->num_redirects >= 8)
        return false;

    if (embed->widget->class->address.type != RESOURCE_ADDRESS_HTTP)
        /* a static or CGI widget cannot send redirects */
        return false;

    p = widget_relative_uri(embed->pool, embed->widget, true,
                            location, strlen(location),
                            &strref_buffer);
    if (p == NULL)
        return false;

    session = embed->widget->class->stateful
        ? session_get(embed->env->session_id)
        : NULL;
    widget_copy_from_location(embed->widget, session,
                              p->data, p->length, embed->pool);
    if (session != NULL)
        session_put(session);

    ++embed->num_redirects;

    address = resource_address_apply(embed->pool,
                                     widget_address(embed->widget),
                                     location, strlen(location),
                                     &address_buffer);
    if (address == NULL)
        return false;

    if (body != NULL)
        istream_close(body);

    headers = widget_request_headers(embed, false);

    resource_get(global_http_cache, global_tcp_stock, global_fcgi_stock,
                 global_delegate_stock,
                 embed->pool,
                 HTTP_METHOD_GET, address, headers, NULL,
                 &widget_response_handler, embed,
                 embed->async_ref);

    return true;
}

static bool
processable(const struct strmap *headers)
{
    const char *content_type;

    content_type = strmap_get_checked(headers, "content-type");
    return content_type != NULL &&
        (strncmp(content_type, "text/html", 9) == 0 ||
         strncmp(content_type, "text/xml", 8) == 0);
}

/**
 * The widget response is going to be embedded into a template; check
 * its content type and run the processor (if applicable).
 */
static void
widget_response_process(struct embed *embed, http_status_t status,
                        struct strmap *headers, istream_t body,
                        unsigned options)
{
    if (body == NULL) {
        daemon_log(2, "widget '%s' didn't send a response body\n",
                   widget_path(embed->widget));
        http_response_handler_invoke_abort(&embed->handler_ref);
        return;
    }

    if (!processable(headers)) {
        daemon_log(2, "widget '%s' sent non-HTML response\n",
                   widget_path(embed->widget));
        http_response_handler_invoke_abort(&embed->handler_ref);
        return;
    }

    processor_new(embed->pool, status, headers, body,
                  embed->widget, embed->env, options,
                  &widget_response_handler, embed,
                  embed->async_ref);
}

/**
 * Apply a transformation to the widget response and hand it back to
 * widget_response_handler.
 */
static void
widget_response_transform(struct embed *embed, http_status_t status,
                          struct strmap *headers, istream_t body,
                          const struct transformation *transformation)
{
    const char *source_tag;

    assert(transformation != NULL);
    assert(embed->transformation == transformation->next);

    switch (transformation->type) {
    case TRANSFORMATION_PROCESS:
        /* processor responses cannot be cached */
        embed->resource_tag = NULL;

        widget_response_process(embed, status, headers, body,
                                transformation->u.processor.options);
        break;

    case TRANSFORMATION_FILTER:
        source_tag = resource_tag_append_etag(embed->pool,
                                             embed->resource_tag, headers);
        embed->resource_tag = source_tag != NULL
            ? resource_address_id(&transformation->u.filter, embed->pool)
            : NULL;

        filter_cache_request(global_filter_cache, embed->pool,
                             &transformation->u.filter,
                             source_tag, headers, body,
                             &widget_response_handler, embed,
                             embed->async_ref);
        break;
    }
}

/**
 * A response was received from the widget server; apply
 * transformations (if enabled) and return it to our handler.  This
 * function will be called (semi-)recursively for every transformation
 * in the chain.
 */
static void
widget_response_dispatch(struct embed *embed, http_status_t status,
                         struct strmap *headers, istream_t body)
{
    const struct transformation *transformation = embed->transformation;

    if (transformation != NULL) {
        /* transform this response */

        embed->transformation = transformation->next;

        widget_response_transform(embed, status, headers,
                                  body, transformation);
    } else {
        /* no transformation left */

        /* finally pass the response to our handler */
        http_response_handler_invoke_response(&embed->handler_ref,
                                              status, headers, body);
    }
}

static void
widget_collect_cookies(struct cookie_jar *jar, const struct strmap *headers,
                       const char *host_and_port)
{
    const char *key = "set-cookie2";
    const char *cookies = strmap_get(headers, key);

    if (cookies == NULL) {
        key = "set-cookie";
        cookies = strmap_get(headers, key);
        if (cookies == NULL)
            return;
    }

    do {
        cookie_jar_set_cookie2(jar, cookies, host_and_port);

        cookies = strmap_get_next(headers, key, cookies);
    } while (cookies != NULL);
}

static void
widget_response_response(http_status_t status, struct strmap *headers,
                         istream_t body, void *ctx)
{
    struct embed *embed = ctx;
    /*const char *translate;*/

    if (headers != NULL) {
        if (embed->host_and_port != NULL) {
            struct session *session = session_get(embed->env->session_id);
            if (session != NULL) {
                widget_collect_cookies(session->cookies, headers,
                                       embed->host_and_port);
                session_put(session);
            }
        }

        /*
        translate = strmap_get(headers, "x-cm4all-beng-translate");
        if (translate != NULL) {
            struct session *session = session_get(embed->env->session_id);
            if (session != NULL)
                session->translate = d_strdup(session->pool, translate);
            session_put(session);
        }
        */

        if (http_status_is_redirect(status)) {
            const char *location = strmap_get(headers, "location");
            if (location != NULL &&
                widget_response_redirect(embed, location, body)) {
                return;
            }
        }
    }

    widget_response_dispatch(embed, status, headers, body);
}

static void
widget_response_abort(void *ctx)
{
    struct embed *embed = ctx;

    http_response_handler_invoke_abort(&embed->handler_ref);
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
    const struct transformation_view *view;
    struct embed *embed;
    struct strmap *headers;
    const struct resource_address *address;

    assert(widget != NULL);
    assert(widget->class != NULL);

    view = transformation_view_lookup(widget->class->views,
                                      widget_get_view_name(widget));
    if (view == NULL) {
        daemon_log(3, "unknown view name for class '%s': '%s'\n",
                   widget->class_name, widget_get_view_name(widget));
        http_response_handler_direct_abort(handler, handler_ctx);
        return;
    }

    embed = p_malloc(pool, sizeof(*embed));
    embed->pool = pool;

    embed->num_redirects = 0;
    embed->widget = widget;
    embed->env = env;
    embed->host_and_port =
        embed->widget->class->address.type == RESOURCE_ADDRESS_HTTP
        ? uri_host_and_port(pool, embed->widget->class->address.u.http->uri)
        : NULL;
    embed->transformation = embed->widget->from_request.raw
        ? NULL : view->transformation;

    headers = widget_request_headers(embed, widget->from_request.body != NULL);

    http_response_handler_set(&embed->handler_ref, handler, handler_ctx);
    embed->async_ref = async_ref;

    address = widget_address(widget);
    embed->resource_tag = resource_address_id(address, pool);

    resource_get(global_http_cache, global_tcp_stock, global_fcgi_stock,
                 global_delegate_stock,
                 pool,
                 widget->from_request.method,
                 address,
                 headers,
                 widget->from_request.body,
                 &widget_response_handler, embed, async_ref);
}
