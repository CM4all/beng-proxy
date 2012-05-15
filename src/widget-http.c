/*
 * Send HTTP requests to a widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-http.h"
#include "http-response.h"
#include "processor.h"
#include "css_processor.h"
#include "text_processor.h"
#include "penv.h"
#include "widget.h"
#include "widget-class.h"
#include "widget-lookup.h"
#include "session.h"
#include "cookie-client.h"
#include "async.h"
#include "dpool.h"
#include "get.h"
#include "fcache.h"
#include "header-writer.h"
#include "header-forward.h"
#include "transformation.h"
#include "global.h"
#include "resource-tag.h"
#include "uri-extract.h"
#include "strmap.h"
#include "istream.h"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

struct embed {
    struct pool *pool;

    unsigned num_redirects;

    struct widget *widget;
    const char *lookup_id;

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

    const struct widget_lookup_handler *lookup_handler;
    void *lookup_handler_ctx;

    struct http_response_handler_ref handler_ref;
    struct async_operation operation;
    struct async_operation_ref async_ref;
};

static inline GQuark
widget_quark(void)
{
    return g_quark_from_static_string("widget");
}

static struct session *
session_get_if_stateful(const struct embed *embed)
{
    return embed->widget->class->stateful
        ? session_get(embed->env->session_id)
        : NULL;
}

static const char *
widget_uri(struct widget *widget)
{
    const struct resource_address *address = widget_address(widget);
    if (address == NULL)
        return NULL;

    return resource_address_uri_path(address);
}

static struct strmap *
widget_request_headers(struct embed *embed, const struct widget_view *view,
                       bool exclude_host, bool with_body)
{
    struct widget *widget = embed->widget;
    struct strmap *headers;
    struct session *session;

    session = session_get(embed->env->session_id);

    headers = forward_request_headers(embed->pool, embed->env->request_headers,
                                      embed->env->local_host,
                                      embed->env->remote_host,
                                      exclude_host, with_body,
                                      false, false,
                                      &view->request_header_forward,
                                      session,
                                      embed->host_and_port,
                                      widget_uri(widget));

    if (session != NULL)
        session_put(session);

    if (widget->class->info_headers) {
        if (widget->id != NULL)
            strmap_add(headers, "x-cm4all-widget-id", widget->id);

        if (widget->class_name != NULL)
            strmap_add(headers, "x-cm4all-widget-type", widget->class_name);

        const char *prefix = widget_prefix(widget);
        if (prefix != NULL)
            strmap_add(headers, "x-cm4all-widget-prefix", prefix);
    }

    if (widget->headers != NULL) {
        /* copy HTTP request headers from template */
        const struct strmap_pair *pair;

        strmap_rewind(widget->headers);

        while ((pair = strmap_next(widget->headers)) != NULL)
            strmap_add(headers,
                       p_strdup(embed->pool, pair->key),
                       p_strdup(embed->pool, pair->value));
    }

    return headers;
}

static const struct http_response_handler widget_response_handler;

static bool
widget_response_redirect(struct embed *embed, const char *location,
                         struct istream *body)
{
    struct widget *widget = embed->widget;
    const struct resource_address *address;
    struct session *session;
    struct strmap *headers;
    struct strref strref_buffer;
    const struct strref *p;
    struct resource_address address_buffer;

    if (embed->num_redirects >= 8)
        return false;

    const struct widget_view *view = widget_get_view(widget);

    if (view == NULL || view->address.type != RESOURCE_ADDRESS_HTTP)
        /* a static or CGI widget cannot send redirects */
        return false;

    p = widget_relative_uri(embed->pool, widget, true,
                            location, strlen(location),
                            &strref_buffer);
    if (p == NULL)
        return false;

    session = session_get_if_stateful(embed);
    widget_copy_from_location(widget, session,
                              p->data, p->length, embed->pool);
    if (session != NULL)
        session_put(session);

    ++embed->num_redirects;

    address = resource_address_apply(embed->pool, widget_address(widget),
                                     location, strlen(location),
                                     &address_buffer);
    if (address == NULL)
        return false;

    if (body != NULL)
        istream_close_unused(body);

    headers = widget_request_headers(embed, view,
                                     address->type == RESOURCE_ADDRESS_HTTP,
                                     false);

    resource_get(global_http_cache, global_tcp_balancer,
                 global_fcgi_stock, global_was_stock, global_delegate_stock,
                 embed->pool, session_id_low(embed->env->session_id),
                 HTTP_METHOD_GET, address, HTTP_STATUS_OK, headers, NULL,
                 &widget_response_handler, embed,
                 &embed->async_ref);

    return true;
}

static bool
processable(const struct strmap *headers)
{
    const char *content_type;

    content_type = strmap_get_checked(headers, "content-type");
    return content_type != NULL &&
        (strncmp(content_type, "text/html", 9) == 0 ||
         strncmp(content_type, "text/xml", 8) == 0 ||
         strncmp(content_type, "application/xhtml+xml", 21) == 0);
}

static void
widget_response_dispatch(struct embed *embed, http_status_t status,
                         struct strmap *headers, struct istream *body);

static void
widget_dispatch_error(struct embed *embed, GError *error)
{
    assert(embed != NULL);
    assert(error != NULL);

    if (embed->lookup_id != NULL)
        embed->lookup_handler->error(error, embed->lookup_handler_ctx);
    else
        http_response_handler_invoke_abort(&embed->handler_ref, error);
}

/**
 * The widget response is going to be embedded into a template; check
 * its content type and run the processor (if applicable).
 */
static void
widget_response_process(struct embed *embed, http_status_t status,
                        struct strmap *headers, struct istream *body,
                        unsigned options)
{
    struct widget *widget = embed->widget;

    if (body == NULL) {
        GError *error =
            g_error_new(widget_quark(), 0,
                        "widget '%s' didn't send a response body",
                        widget_path(widget));
        widget_dispatch_error(embed, error);
        return;
    }

    if (!processable(headers)) {
        istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), 0,
                        "widget '%s' sent non-HTML response",
                        widget_path(widget));
        widget_dispatch_error(embed, error);
        return;
    }

    if (embed->lookup_id != NULL)
        processor_lookup_widget(embed->pool, status, body,
                                widget, embed->lookup_id,
                                embed->env, options,
                                embed->lookup_handler,
                                embed->lookup_handler_ctx,
                                &embed->async_ref);
    else {
        body = processor_process(embed->pool, body,
                                 widget, embed->env, options);

        widget_response_dispatch(embed, status, headers, body);
    }
}

static bool
css_processable(const struct strmap *headers)
{
    const char *content_type;

    content_type = strmap_get_checked(headers, "content-type");
    return content_type != NULL &&
        strncmp(content_type, "text/css", 8) == 0;
}

static void
widget_response_process_css(struct embed *embed, http_status_t status,
                            struct strmap *headers, struct istream *body,
                            unsigned options)
{
    struct widget *widget = embed->widget;

    if (body == NULL) {
        GError *error =
            g_error_new(widget_quark(), 0,
                        "widget '%s' didn't send a response body",
                        widget_path(widget));
        widget_dispatch_error(embed, error);
        return;
    }

    if (!css_processable(headers)) {
        istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), 0,
                        "widget '%s' sent non-CSS response",
                        widget_path(widget));
        widget_dispatch_error(embed, error);
        return;
    }

    body = css_processor(embed->pool, body, widget, embed->env, options);
    widget_response_dispatch(embed, status, headers, body);
}

static void
widget_response_process_text(struct embed *embed, http_status_t status,
                             struct strmap *headers, struct istream *body)
{
    const struct widget *widget = embed->widget;

    if (body == NULL) {
        GError *error =
            g_error_new(widget_quark(), 0,
                        "widget '%s' didn't send a response body",
                        widget_path(widget));
        widget_dispatch_error(embed, error);
        return;
    }

    if (!text_processor_allowed(headers)) {
        istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), 0,
                        "widget '%s' sent non-text response",
                        widget_path(widget));
        widget_dispatch_error(embed, error);
        return;
    }

    body = text_processor(embed->pool, body, widget, embed->env);
    widget_response_dispatch(embed, status, headers, body);
}

static void
widget_response_apply_filter(struct embed *embed, http_status_t status,
                             struct strmap *headers, struct istream *body,
                             const struct resource_address *filter)
{
    const char *source_tag;
    source_tag = resource_tag_append_etag(embed->pool,
                                          embed->resource_tag, headers);
    embed->resource_tag = source_tag != NULL
        ? p_strcat(embed->pool, source_tag, "|",
                   resource_address_id(filter, embed->pool),
                   NULL)
        : NULL;

#ifdef SPLICE
    if (body != NULL)
        body = istream_pipe_new(embed->pool, body, global_pipe_stock);
#endif

    filter_cache_request(global_filter_cache, embed->pool, filter,
                         source_tag, status, headers, body,
                         &widget_response_handler, embed,
                         &embed->async_ref);
}

/**
 * Apply a transformation to the widget response and hand it back to
 * widget_response_handler.
 */
static void
widget_response_transform(struct embed *embed, http_status_t status,
                          struct strmap *headers, struct istream *body,
                          const struct transformation *transformation)
{
    const char *p;

    assert(transformation != NULL);
    assert(embed->transformation == transformation->next);

    p = strmap_get_checked(headers, "content-encoding");
    if (p != NULL && strcmp(p, "identity") != 0) {
        if (body != NULL)
            istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), 0,
                        "widget '%s' sent non-identity response, "
                        "cannot transform",
                        widget_path(embed->widget));

        widget_dispatch_error(embed, error);
        return;
    }

    switch (transformation->type) {
    case TRANSFORMATION_PROCESS:
        /* processor responses cannot be cached */
        embed->resource_tag = NULL;

        widget_response_process(embed, status, headers, body,
                                transformation->u.processor.options);
        break;

    case TRANSFORMATION_PROCESS_CSS:
        /* processor responses cannot be cached */
        embed->resource_tag = NULL;

        widget_response_process_css(embed, status, headers, body,
                                    transformation->u.css_processor.options);
        break;

    case TRANSFORMATION_PROCESS_TEXT:
        /* processor responses cannot be cached */
        embed->resource_tag = NULL;

        widget_response_process_text(embed, status, headers, body);
        break;

    case TRANSFORMATION_FILTER:
        widget_response_apply_filter(embed, status, headers, body,
                                     &transformation->u.filter);
        break;
    }
}

static bool
widget_transformation_enabled(const struct widget *widget,
                              http_status_t status)
{
    return http_status_is_success(status) ||
        (http_status_is_client_error(status) &&
         widget_get_view(widget) != NULL &&
         widget_get_view(widget)->filter_4xx);
}

/**
 * A response was received from the widget server; apply
 * transformations (if enabled) and return it to our handler.  This
 * function will be called (semi-)recursively for every transformation
 * in the chain.
 */
static void
widget_response_dispatch(struct embed *embed, http_status_t status,
                         struct strmap *headers, struct istream *body)
{
    const struct transformation *transformation = embed->transformation;

    if (transformation != NULL &&
        widget_transformation_enabled(embed->widget, status)) {
        /* transform this response */

        embed->transformation = transformation->next;

        widget_response_transform(embed, status, headers,
                                  body, transformation);
    } else if (embed->lookup_id != NULL) {
        if (body != NULL)
            istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), 0,
                        "Cannot process container widget response of %s",
                        widget_path(embed->widget));
        embed->lookup_handler->error(error, embed->lookup_handler_ctx);
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
        cookie_jar_set_cookie2(jar, cookies, host_and_port, NULL);

        cookies = strmap_get_next(headers, key, cookies);
    } while (cookies != NULL);
}

static void
widget_response_response(http_status_t status, struct strmap *headers,
                         struct istream *body, void *ctx)
{
    struct embed *embed = ctx;
    /*const char *translate;*/

    if (headers != NULL) {
        if (embed->widget->class->dump_headers) {
            daemon_log(4, "response headers from widget '%s'\n",
                       widget_path(embed->widget));
            strmap_rewind(headers);
            const struct strmap_pair *pair;
            while ((pair = strmap_next(headers)) != NULL)
                daemon_log(4, "  %s: %s\n", pair->key, pair->value);
        }

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

        /* select a new view? */

        const char *view_name = strmap_get(headers, "x-cm4all-view");
        if (view_name != NULL) {
            /* yes, look it up in the class */

            const struct widget_view *view =
                widget_view_lookup(&embed->widget->class->views, view_name);
            if (view == NULL) {
                /* the view specified in the response header does not
                   exist, bail out */

                if (body != NULL)
                    istream_close_unused(body);

                GError *error =
                    g_error_new(widget_quark(), 0,
                                "No such view: %s", view_name);
                widget_dispatch_error(embed, error);
                return;
            }

            /* install the new view */
            embed->transformation = view->transformation;
        }
    }

    widget_response_dispatch(embed, status, headers, body);
}

static void
widget_response_abort(GError *error, void *ctx)
{
    struct embed *embed = ctx;

    widget_dispatch_error(embed, error);
}

static const struct http_response_handler widget_response_handler = {
    .response = widget_response_response,
    .abort = widget_response_abort,
};


/*
 * async operation
 *
 */

static struct embed *
async_to_embed(struct async_operation *ao)
{
    return (struct embed *)(((char*)ao) - offsetof(struct embed, operation));
}

static void
widget_http_abort(struct async_operation *ao)
{
    struct embed *embed = async_to_embed(ao);

    widget_cancel(embed->widget);

    async_abort(&embed->async_ref);
}

static const struct async_operation_class widget_http_operation = {
    .abort = widget_http_abort,
};


/*
 * constructor
 *
 */

void
widget_http_request(struct pool *pool, struct widget *widget,
                    struct processor_env *env,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    struct embed *embed;
    struct strmap *headers;
    const struct resource_address *address;

    assert(widget != NULL);
    assert(widget->class != NULL);

    const struct widget_view *view = widget_get_view(widget);
    if (view == NULL) {
        GError *error =
            g_error_new(widget_quark(), 0,
                        "unknown view name for class '%s': '%s'",
                        widget->class_name, widget_get_view_name(widget));
        widget_cancel(widget);
        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    embed = p_malloc(pool, sizeof(*embed));
    embed->pool = pool;

    embed->num_redirects = 0;
    embed->widget = widget;
    embed->lookup_id = NULL;
    embed->env = env;
    embed->host_and_port = widget->class->cookie_host != NULL
        ? widget->class->cookie_host
        : resource_address_host_and_port(&view->address, pool);
    embed->transformation = embed->widget->from_request.raw
        ? NULL : view->transformation;

    headers = widget_request_headers(embed, view,
                                     widget_address(embed->widget)->type == RESOURCE_ADDRESS_HTTP,
                                     widget->from_request.body != NULL);

    if (widget->class->dump_headers) {
        daemon_log(4, "request headers for widget '%s'\n", widget_path(widget));
        strmap_rewind(headers);
        const struct strmap_pair *pair;
        while ((pair = strmap_next(headers)) != NULL)
            daemon_log(4, "  %s: %s\n", pair->key, pair->value);
    }

    http_response_handler_set(&embed->handler_ref, handler, handler_ctx);

    async_init(&embed->operation, &widget_http_operation);
    async_ref_set(async_ref, &embed->operation);

    address = widget_address(widget);
    embed->resource_tag = resource_address_id(address, pool);

    resource_get(global_http_cache, global_tcp_balancer,
                 global_fcgi_stock, global_was_stock, global_delegate_stock,
                 pool, session_id_low(embed->env->session_id),
                 widget->from_request.method,
                 address,
                 HTTP_STATUS_OK, headers,
                 widget->from_request.body,
                 &widget_response_handler, embed, &embed->async_ref);
}

void
widget_http_lookup(struct pool *pool, struct widget *widget, const char *id,
                   struct processor_env *env,
                   const struct widget_lookup_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    struct embed *embed;
    struct strmap *headers;
    const struct resource_address *address;

    assert(widget != NULL);
    assert(widget->class != NULL);
    assert(id != NULL);
    assert(handler != NULL);
    assert(handler->found != NULL);
    assert(handler->not_found != NULL);
    assert(handler->error != NULL);

    const struct widget_view *view = widget_get_view(widget);
    if (view == NULL) {
        GError *error =
            g_error_new(widget_quark(), 0,
                        "unknown view name for class '%s': '%s'",
                        widget->class_name, widget_get_view_name(widget));
        handler->error(error, handler_ctx);
        return;
    }

    embed = p_malloc(pool, sizeof(*embed));
    embed->pool = pool;

    embed->num_redirects = 0;
    embed->widget = widget;
    embed->lookup_id = id;
    embed->env = env;
    embed->host_and_port = widget->class->cookie_host != NULL
        ? widget->class->cookie_host
        : resource_address_host_and_port(&view->address, pool);
    embed->transformation = view->transformation;

    headers = widget_request_headers(embed, view,
                                     widget_address(embed->widget)->type == RESOURCE_ADDRESS_HTTP,
                                     widget->from_request.body != NULL);

    embed->lookup_handler = handler;
    embed->lookup_handler_ctx = handler_ctx;

    async_init(&embed->operation, &widget_http_operation);
    async_ref_set(async_ref, &embed->operation);

    address = widget_address(widget);
    embed->resource_tag = resource_address_id(address, pool);

    resource_get(global_http_cache, global_tcp_balancer,
                 global_fcgi_stock, global_was_stock, global_delegate_stock,
                 pool, session_id_low(embed->env->session_id),
                 widget->from_request.method,
                 address,
                 HTTP_STATUS_OK, headers,
                 widget->from_request.body,
                 &widget_response_handler, embed, &embed->async_ref);
}
