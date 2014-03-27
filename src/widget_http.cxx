/*
 * Send HTTP requests to a widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_http.hxx"
#include "http_response.h"
#include "pheaders.h"
#include "processor.h"
#include "css_processor.h"
#include "text_processor.hxx"
#include "penv.h"
#include "widget.h"
#include "widget_class.hxx"
#include "widget_request.hxx"
#include "widget-lookup.h"
#include "widget-quark.h"
#include "session.h"
#include "cookie_client.h"
#include "async.h"
#include "dpool.h"
#include "get.h"
#include "fcache.h"
#include "header-writer.h"
#include "header-forward.h"
#include "transformation.hxx"
#include "global.h"
#include "resource-tag.h"
#include "uri-extract.h"
#include "strmap.h"
#include "istream.h"
#include "cast.hxx"

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

static struct session *
session_get_if_stateful(const struct embed *embed)
{
    return embed->widget->cls->stateful
        ? session_get(embed->env->session_id)
        : nullptr;
}

static const char *
widget_uri(struct widget *widget)
{
    const struct resource_address *address = widget_address(widget);
    if (address == nullptr)
        return nullptr;

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

    if (session != nullptr)
        session_put(session);

    if (widget->cls->info_headers) {
        if (widget->id != nullptr)
            strmap_add(headers, "x-cm4all-widget-id", widget->id);

        if (widget->class_name != nullptr)
            strmap_add(headers, "x-cm4all-widget-type", widget->class_name);

        const char *prefix = widget_prefix(widget);
        if (prefix != nullptr)
            strmap_add(headers, "x-cm4all-widget-prefix", prefix);
    }

    if (widget->headers != nullptr) {
        /* copy HTTP request headers from template */
        const struct strmap_pair *pair;

        strmap_rewind(widget->headers);

        while ((pair = strmap_next(widget->headers)) != nullptr)
            strmap_add(headers,
                       p_strdup(embed->pool, pair->key),
                       p_strdup(embed->pool, pair->value));
    }

    return headers;
}

extern const struct http_response_handler widget_response_handler;

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

    const struct widget_view *view = widget_get_address_view(widget);
    assert(view != nullptr);

    if (view->address.type != RESOURCE_ADDRESS_HTTP)
        /* a static or CGI widget cannot send redirects */
        return false;

    p = widget_relative_uri(embed->pool, widget, true,
                            location, strlen(location),
                            &strref_buffer);
    if (p == nullptr)
        return false;

    session = session_get_if_stateful(embed);
    widget_copy_from_location(widget, session,
                              p->data, p->length, embed->pool);
    if (session != nullptr)
        session_put(session);

    ++embed->num_redirects;

    address = resource_address_apply(embed->pool, widget_address(widget),
                                     location, strlen(location),
                                     &address_buffer);
    if (address == nullptr)
        return false;

    if (body != nullptr)
        istream_close_unused(body);

    headers = widget_request_headers(embed, view,
                                     address->type == RESOURCE_ADDRESS_HTTP ||
                                     address->type == RESOURCE_ADDRESS_LHTTP,
                                     false);

    resource_get(global_http_cache, global_tcp_balancer,
                 global_lhttp_stock,
                 global_fcgi_stock, global_was_stock, global_delegate_stock,
                 global_nfs_cache,
                 embed->pool, session_id_low(embed->env->session_id),
                 HTTP_METHOD_GET, address, HTTP_STATUS_OK, headers, nullptr,
                 &widget_response_handler, embed,
                 &embed->async_ref);

    return true;
}

static void
widget_response_dispatch(struct embed *embed, http_status_t status,
                         struct strmap *headers, struct istream *body);

static void
widget_dispatch_error(struct embed *embed, GError *error)
{
    assert(embed != nullptr);
    assert(error != nullptr);

    if (embed->lookup_id != nullptr)
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

    if (body == nullptr) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_EMPTY,
                        "widget '%s' didn't send a response body",
                        widget_path(widget));
        widget_dispatch_error(embed, error);
        return;
    }

    if (!processable(headers)) {
        istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                        "widget '%s' sent non-HTML response",
                        widget_path(widget));
        widget_dispatch_error(embed, error);
        return;
    }

    if (embed->lookup_id != nullptr)
        processor_lookup_widget(embed->pool, body,
                                widget, embed->lookup_id,
                                embed->env, options,
                                embed->lookup_handler,
                                embed->lookup_handler_ctx,
                                &embed->async_ref);
    else {
        headers = processor_header_forward(embed->pool, headers);
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
    return content_type != nullptr &&
        strncmp(content_type, "text/css", 8) == 0;
}

static void
widget_response_process_css(struct embed *embed, http_status_t status,
                            struct strmap *headers, struct istream *body,
                            unsigned options)
{
    struct widget *widget = embed->widget;

    if (body == nullptr) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_EMPTY,
                        "widget '%s' didn't send a response body",
                        widget_path(widget));
        widget_dispatch_error(embed, error);
        return;
    }

    if (!css_processable(headers)) {
        istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                        "widget '%s' sent non-CSS response",
                        widget_path(widget));
        widget_dispatch_error(embed, error);
        return;
    }

    headers = processor_header_forward(embed->pool, headers);
    body = css_processor(embed->pool, body, widget, embed->env, options);
    widget_response_dispatch(embed, status, headers, body);
}

static void
widget_response_process_text(struct embed *embed, http_status_t status,
                             struct strmap *headers, struct istream *body)
{
    const struct widget *widget = embed->widget;

    if (body == nullptr) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_EMPTY,
                        "widget '%s' didn't send a response body",
                        widget_path(widget));
        widget_dispatch_error(embed, error);
        return;
    }

    if (!text_processor_allowed(headers)) {
        istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                        "widget '%s' sent non-text response",
                        widget_path(widget));
        widget_dispatch_error(embed, error);
        return;
    }

    headers = processor_header_forward(embed->pool, headers);
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
    embed->resource_tag = source_tag != nullptr
        ? p_strcat(embed->pool, source_tag, "|",
                   resource_address_id(filter, embed->pool),
                   nullptr)
        : nullptr;

#ifdef SPLICE
    if (body != nullptr)
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

    assert(transformation != nullptr);
    assert(embed->transformation == transformation->next);

    p = strmap_get_checked(headers, "content-encoding");
    if (p != nullptr && strcmp(p, "identity") != 0) {
        if (body != nullptr)
            istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_UNSUPPORTED_ENCODING,
                        "widget '%s' sent non-identity response, "
                        "cannot transform",
                        widget_path(embed->widget));

        widget_dispatch_error(embed, error);
        return;
    }

    switch (transformation->type) {
    case transformation::TRANSFORMATION_PROCESS:
        /* processor responses cannot be cached */
        embed->resource_tag = nullptr;

        widget_response_process(embed, status, headers, body,
                                transformation->u.processor.options);
        break;

    case transformation::TRANSFORMATION_PROCESS_CSS:
        /* processor responses cannot be cached */
        embed->resource_tag = nullptr;

        widget_response_process_css(embed, status, headers, body,
                                    transformation->u.css_processor.options);
        break;

    case transformation::TRANSFORMATION_PROCESS_TEXT:
        /* processor responses cannot be cached */
        embed->resource_tag = nullptr;

        widget_response_process_text(embed, status, headers, body);
        break;

    case transformation::TRANSFORMATION_FILTER:
        widget_response_apply_filter(embed, status, headers, body,
                                     &transformation->u.filter);
        break;
    }
}

static bool
widget_transformation_enabled(const struct widget *widget,
                              http_status_t status)
{
    assert(widget_get_transformation_view(widget) != nullptr);

    return http_status_is_success(status) ||
        (http_status_is_client_error(status) &&
         widget_get_transformation_view(widget)->filter_4xx);
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

    if (transformation != nullptr &&
        widget_transformation_enabled(embed->widget, status)) {
        /* transform this response */

        embed->transformation = transformation->next;

        widget_response_transform(embed, status, headers,
                                  body, transformation);
    } else if (embed->lookup_id != nullptr) {
        if (body != nullptr)
            istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_NOT_A_CONTAINER,
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
    const struct strmap_pair *cookies =
        strmap_lookup_first(headers, "set-cookie2");
    if (cookies == nullptr) {
        cookies = strmap_lookup_first(headers, "set-cookie");
        if (cookies == nullptr)
            return;
    }

    do {
        cookie_jar_set_cookie2(jar, cookies->value, host_and_port, nullptr);

        cookies = strmap_lookup_next(cookies);
    } while (cookies != nullptr);
}

static bool
widget_update_view(struct embed *embed, struct strmap *headers,
                   GError **error_r)
{
    struct widget *widget = embed->widget;

    const char *view_name = strmap_get(headers, "x-cm4all-view");
    if (view_name != nullptr) {
        /* yes, look it up in the class */

        const struct widget_view *view =
            widget_class_view_lookup(widget->cls, view_name);
        if (view == nullptr) {
            /* the view specified in the response header does not
               exist, bail out */

            g_set_error(error_r, widget_quark(), WIDGET_ERROR_NO_SUCH_VIEW,
                        "No such view: %s", view_name);
            return false;
        }

        /* install the new view */
        embed->transformation = view->transformation;
    } else if (widget->from_request.unauthorized_view &&
               processable(headers) &&
               !widget_is_container(widget)) {
        /* postponed check from proxy_widget_continue(): an
           unauthorized view was selected, which is only allowed if
           the output is not processable; if it is, we may expose
           internal widget parameters */

        g_set_error(error_r, widget_quark(), WIDGET_ERROR_FORBIDDEN,
                    "view '%s' of widget class '%s' cannot be requested "
                    "because the response is processable",
                    widget_get_transformation_view(widget)->name, widget->class_name);
        return false;
    }

    return true;
}

static void
widget_response_response(http_status_t status, struct strmap *headers,
                         struct istream *body, void *ctx)
{
    struct embed *embed = (struct embed *)ctx;
    struct widget *widget = embed->widget;
    /*const char *translate;*/

    if (headers != nullptr) {
        if (widget->cls->dump_headers) {
            daemon_log(4, "response headers from widget '%s'\n",
                       widget_path(widget));
            strmap_rewind(headers);
            const struct strmap_pair *pair;
            while ((pair = strmap_next(headers)) != nullptr)
                daemon_log(4, "  %s: %s\n", pair->key, pair->value);
        }

        if (embed->host_and_port != nullptr) {
            struct session *session = session_get(embed->env->session_id);
            if (session != nullptr) {
                widget_collect_cookies(session->cookies, headers,
                                       embed->host_and_port);
                session_put(session);
            }
        }

        /*
        translate = strmap_get(headers, "x-cm4all-beng-translate");
        if (translate != nullptr) {
            struct session *session = session_get(embed->env->session_id);
            if (session != nullptr)
                session->translate = d_strdup(session->pool, translate);
            session_put(session);
        }
        */

        if (http_status_is_redirect(status)) {
            const char *location = strmap_get(headers, "location");
            if (location != nullptr &&
                widget_response_redirect(embed, location, body)) {
                return;
            }
        }

        /* select a new view? */

        GError *error = nullptr;
        if (!widget_update_view(embed, headers, &error)) {
            if (body != nullptr)
                istream_close_unused(body);

            widget_dispatch_error(embed, error);
            return;
        }
    }

#ifndef NDEBUG
    /* temporary kludge for widgets that are still using the
       "mode=proxy" feature that has been deprecated since beng-proxy
       0.4 */
    if (embed->transformation->HasProcessor() &&
        g_getenv("VERBATIM_UNPROCESSABLE") != nullptr &&
        !processable(headers))
        embed->transformation = nullptr;
#endif

    if (widget->session_save_pending &&
        embed->transformation->HasProcessor()) {
        struct session *session = session_get(embed->env->session_id);
        if (session != nullptr) {
            widget_save_session(widget, session);
            session_put(session);
        }
    }

    widget_response_dispatch(embed, status, headers, body);
}

static void
widget_response_abort(GError *error, void *ctx)
{
    struct embed *embed = (struct embed *)ctx;

    widget_dispatch_error(embed, error);
}

const struct http_response_handler widget_response_handler = {
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
    return ContainerCast(ao, struct embed, operation);
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
    struct strmap *headers;
    const struct resource_address *address;

    assert(widget != nullptr);
    assert(widget->cls != nullptr);

    const struct widget_view *a_view = widget_get_address_view(widget);
    assert(a_view != nullptr);

    const struct widget_view *t_view = widget_get_transformation_view(widget);
    assert(t_view != nullptr);

    auto embed = NewFromPool<struct embed>(pool);
    embed->pool = pool;

    embed->num_redirects = 0;
    embed->widget = widget;
    embed->lookup_id = nullptr;
    embed->env = env;
    embed->host_and_port = widget->cls->cookie_host != nullptr
        ? widget->cls->cookie_host
        : resource_address_host_and_port(&a_view->address);
    embed->transformation = t_view->transformation;

    headers = widget_request_headers(embed, a_view,
                                     widget_address(embed->widget)->type == RESOURCE_ADDRESS_HTTP ||
                                     widget_address(embed->widget)->type == RESOURCE_ADDRESS_LHTTP,
                                     widget->from_request.body != nullptr);

    if (widget->cls->dump_headers) {
        daemon_log(4, "request headers for widget '%s'\n", widget_path(widget));
        strmap_rewind(headers);
        const struct strmap_pair *pair;
        while ((pair = strmap_next(headers)) != nullptr)
            daemon_log(4, "  %s: %s\n", pair->key, pair->value);
    }

    http_response_handler_set(&embed->handler_ref, handler, handler_ctx);

    async_init(&embed->operation, &widget_http_operation);
    async_ref_set(async_ref, &embed->operation);

    address = widget_address(widget);
    embed->resource_tag = resource_address_id(address, pool);

    struct istream *request_body = widget->from_request.body;
    widget->from_request.body = nullptr;

    resource_get(global_http_cache, global_tcp_balancer,
                 global_lhttp_stock,
                 global_fcgi_stock, global_was_stock, global_delegate_stock,
                 global_nfs_cache,
                 pool, session_id_low(embed->env->session_id),
                 widget->from_request.method,
                 address,
                 HTTP_STATUS_OK, headers,
                 request_body,
                 &widget_response_handler, embed, &embed->async_ref);
}

void
widget_http_lookup(struct pool *pool, struct widget *widget, const char *id,
                   struct processor_env *env,
                   const struct widget_lookup_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    struct strmap *headers;
    const struct resource_address *address;

    assert(widget != nullptr);
    assert(widget->cls != nullptr);
    assert(id != nullptr);
    assert(handler != nullptr);
    assert(handler->found != nullptr);
    assert(handler->not_found != nullptr);
    assert(handler->error != nullptr);

    const struct widget_view *a_view = widget_get_address_view(widget);
    assert(a_view != nullptr);

    const struct widget_view *t_view = widget_get_transformation_view(widget);
    assert(t_view != nullptr);

    auto embed = NewFromPool<struct embed>(pool);
    embed->pool = pool;

    embed->num_redirects = 0;
    embed->widget = widget;
    embed->lookup_id = id;
    embed->env = env;
    embed->host_and_port = widget->cls->cookie_host != nullptr
        ? widget->cls->cookie_host
        : resource_address_host_and_port(&a_view->address);
    embed->transformation = t_view->transformation;

    headers = widget_request_headers(embed, a_view,
                                     widget_address(embed->widget)->type == RESOURCE_ADDRESS_HTTP ||
                                     widget_address(embed->widget)->type == RESOURCE_ADDRESS_LHTTP,
                                     widget->from_request.body != nullptr);

    embed->lookup_handler = handler;
    embed->lookup_handler_ctx = handler_ctx;

    async_init(&embed->operation, &widget_http_operation);
    async_ref_set(async_ref, &embed->operation);

    address = widget_address(widget);
    embed->resource_tag = resource_address_id(address, pool);

    struct istream *request_body = widget->from_request.body;
    widget->from_request.body = nullptr;

    resource_get(global_http_cache, global_tcp_balancer,
                 global_lhttp_stock,
                 global_fcgi_stock, global_was_stock, global_delegate_stock,
                 global_nfs_cache,
                 pool, session_id_low(embed->env->session_id),
                 widget->from_request.method,
                 address,
                 HTTP_STATUS_OK, headers,
                 request_body,
                 &widget_response_handler, embed, &embed->async_ref);
}
