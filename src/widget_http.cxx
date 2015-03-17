/*
 * Send HTTP requests to a widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_http.hxx"
#include "http_response.hxx"
#include "pheaders.hxx"
#include "processor.h"
#include "css_processor.h"
#include "text_processor.hxx"
#include "penv.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "widget_request.hxx"
#include "widget_lookup.hxx"
#include "widget-quark.h"
#include "session.hxx"
#include "cookie_client.hxx"
#include "async.hxx"
#include "dpool.h"
#include "get.hxx"
#include "fcache.hxx"
#include "header_writer.hxx"
#include "header_forward.hxx"
#include "transformation.hxx"
#include "bp_global.hxx"
#include "resource_tag.hxx"
#include "uri-extract.h"
#include "strmap.hxx"
#include "istream.h"
#include "istream_pipe.hxx"
#include "pool.hxx"
#include "suffix_registry.hxx"
#include "address_suffix_registry.hxx"
#include "util/Cast.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

struct embed {
    struct pool &pool;

    unsigned num_redirects = 0;

    struct widget &widget;
    const char *const lookup_id = nullptr;

    struct processor_env &env;
    const char *host_and_port;

    /**
     * the next transformation to be applied to the widget response
     */
    const Transformation *transformation;

    /**
     * An identifier for the source stream of the current
     * transformation.  This is used by the filter cache to address
     * resources.
     */
    const char *resource_tag;

    /**
     * The Content-Type from the suffix registry.
     */
    const char *content_type = nullptr;

    const struct widget_lookup_handler *lookup_handler;
    void *lookup_handler_ctx;

    struct http_response_handler_ref handler_ref;
    struct async_operation operation;
    struct async_operation_ref async_ref;

    embed(struct pool &_pool, struct widget &_widget,
          struct processor_env &_env,
          const struct http_response_handler &_handler,
          void *_handler_ctx,
          struct async_operation_ref &_async_ref)
        :pool(_pool), widget(_widget), env(_env) {
        handler_ref.Set(_handler, _handler_ctx);
        operation.Init2<struct embed>();
        _async_ref.Set(operation);
    }

    embed(struct pool &_pool, struct widget &_widget,
          struct processor_env &_env,
          const char *_lookup_id,
          const struct widget_lookup_handler &_handler,
          void *_handler_ctx,
          struct async_operation_ref &_async_ref)
        :pool(_pool), widget(_widget),
         lookup_id(_lookup_id),
         env(_env),
         lookup_handler(&_handler), lookup_handler_ctx(_handler_ctx) {
        operation.Init2<struct embed>();
        _async_ref.Set(operation);
    }

    bool ContentTypeLookup();
    void SendRequest();

    void Abort() {
        widget_cancel(&widget);
        async_ref.Abort();
    }
};

static struct session *
session_get_if_stateful(const struct embed *embed)
{
    return embed->widget.cls->stateful
        ? session_get(embed->env.session_id)
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
widget_request_headers(struct embed *embed, const WidgetView *view,
                       bool exclude_host, bool with_body)
{
    struct widget &widget = embed->widget;

    auto *session = session_get(embed->env.session_id);

    auto *headers =
        forward_request_headers(embed->pool, embed->env.request_headers,
                                embed->env.local_host,
                                embed->env.remote_host,
                                exclude_host, with_body,
                                widget.from_request.frame && !view->HasProcessor(),
                                widget.from_request.frame && view->transformation == nullptr,
                                widget.from_request.frame && view->transformation == nullptr,
                                view->request_header_forward,
                                embed->env.session_cookie,
                                session,
                                embed->host_and_port,
                                widget_uri(&widget));

    if (session != nullptr)
        session_put(session);

    if (widget.cls->info_headers) {
        if (widget.id != nullptr)
            headers->Add("x-cm4all-widget-id", widget.id);

        if (widget.class_name != nullptr)
            headers->Add("x-cm4all-widget-type", widget.class_name);

        const char *prefix = widget.GetPrefix();
        if (prefix != nullptr)
            headers->Add("x-cm4all-widget-prefix", prefix);
    }

    if (widget.headers != nullptr)
        /* copy HTTP request headers from template */
        for (const auto &i : *widget.headers)
            headers->Add(p_strdup(&embed->pool, i.key),
                         p_strdup(&embed->pool, i.value));

    return headers;
}

extern const struct http_response_handler widget_response_handler;

static bool
widget_response_redirect(struct embed *embed, const char *location,
                         struct istream *body)
{
    struct widget &widget = embed->widget;

    if (embed->num_redirects >= 8)
        return false;

    const WidgetView *view = widget_get_address_view(&widget);
    assert(view != nullptr);

    if (view->address.type != RESOURCE_ADDRESS_HTTP)
        /* a static or CGI widget cannot send redirects */
        return false;

    struct strref strref_buffer;
    const auto *p = widget_relative_uri(&embed->pool, &widget, true,
                                        location, strlen(location),
                                        &strref_buffer);
    if (p == nullptr)
        return false;

    auto *session = session_get_if_stateful(embed);
    widget_copy_from_location(&widget, session,
                              p->data, p->length, &embed->pool);
    if (session != nullptr)
        session_put(session);

    ++embed->num_redirects;

    struct resource_address address_buffer;
    const auto *address =
        resource_address_apply(&embed->pool, widget_address(&widget),
                               location, strlen(location),
                               &address_buffer);
    if (address == nullptr)
        return false;

    if (body != nullptr)
        istream_close_unused(body);

    auto *headers =
        widget_request_headers(embed, view,
                               address->type == RESOURCE_ADDRESS_HTTP ||
                               address->type == RESOURCE_ADDRESS_LHTTP,
                               false);

    resource_get(global_http_cache, global_tcp_balancer,
                 global_lhttp_stock,
                 global_fcgi_stock, global_was_stock, global_delegate_stock,
                 global_nfs_cache,
                 &embed->pool, session_id_low(embed->env.session_id),
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
        embed->handler_ref.InvokeAbort(error);
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
    struct widget &widget = embed->widget;

    if (body == nullptr) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_EMPTY,
                        "widget '%s' didn't send a response body",
                        widget.GetIdPath());
        widget_dispatch_error(embed, error);
        return;
    }

    if (!processable(headers)) {
        istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                        "widget '%s' sent non-HTML response",
                        widget.GetIdPath());
        widget_dispatch_error(embed, error);
        return;
    }

    if (embed->lookup_id != nullptr)
        processor_lookup_widget(&embed->pool, body,
                                &widget, embed->lookup_id,
                                &embed->env, options,
                                embed->lookup_handler,
                                embed->lookup_handler_ctx,
                                &embed->async_ref);
    else {
        headers = processor_header_forward(&embed->pool, headers);
        body = processor_process(&embed->pool, body,
                                 &widget, &embed->env, options);

        widget_response_dispatch(embed, status, headers, body);
    }
}

static bool
css_processable(const struct strmap *headers)
{
    const char *content_type = strmap_get_checked(headers, "content-type");
    return content_type != nullptr &&
        strncmp(content_type, "text/css", 8) == 0;
}

static void
widget_response_process_css(struct embed *embed, http_status_t status,
                            struct strmap *headers, struct istream *body,
                            unsigned options)
{
    struct widget &widget = embed->widget;

    if (body == nullptr) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_EMPTY,
                        "widget '%s' didn't send a response body",
                        widget.GetIdPath());
        widget_dispatch_error(embed, error);
        return;
    }

    if (!css_processable(headers)) {
        istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                        "widget '%s' sent non-CSS response",
                        widget.GetIdPath());
        widget_dispatch_error(embed, error);
        return;
    }

    headers = processor_header_forward(&embed->pool, headers);
    body = css_processor(&embed->pool, body, &widget, &embed->env, options);
    widget_response_dispatch(embed, status, headers, body);
}

static void
widget_response_process_text(struct embed *embed, http_status_t status,
                             struct strmap *headers, struct istream *body)
{
    const struct widget &widget = embed->widget;

    if (body == nullptr) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_EMPTY,
                        "widget '%s' didn't send a response body",
                        widget.GetIdPath());
        widget_dispatch_error(embed, error);
        return;
    }

    if (!text_processor_allowed(headers)) {
        istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                        "widget '%s' sent non-text response",
                        widget.GetIdPath());
        widget_dispatch_error(embed, error);
        return;
    }

    headers = processor_header_forward(&embed->pool, headers);
    body = text_processor(&embed->pool, body, &widget, &embed->env);
    widget_response_dispatch(embed, status, headers, body);
}

static void
widget_response_apply_filter(struct embed *embed, http_status_t status,
                             struct strmap *headers, struct istream *body,
                             const struct resource_address *filter)
{
    const char *source_tag =
        resource_tag_append_etag(&embed->pool, embed->resource_tag, headers);
    embed->resource_tag = source_tag != nullptr
        ? p_strcat(&embed->pool, source_tag, "|",
                   resource_address_id(filter, &embed->pool),
                   nullptr)
        : nullptr;

#ifdef SPLICE
    if (body != nullptr)
        body = istream_pipe_new(&embed->pool, body, global_pipe_stock);
#endif

    filter_cache_request(global_filter_cache, &embed->pool, filter,
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
                          const Transformation *transformation)
{
    assert(transformation != nullptr);
    assert(embed->transformation == transformation->next);

    const char *p = strmap_get_checked(headers, "content-encoding");
    if (p != nullptr && strcmp(p, "identity") != 0) {
        if (body != nullptr)
            istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_UNSUPPORTED_ENCODING,
                        "widget '%s' sent non-identity response, "
                        "cannot transform",
                        embed->widget.GetIdPath());

        widget_dispatch_error(embed, error);
        return;
    }

    switch (transformation->type) {
    case Transformation::Type::PROCESS:
        /* processor responses cannot be cached */
        embed->resource_tag = nullptr;

        widget_response_process(embed, status, headers, body,
                                transformation->u.processor.options);
        break;

    case Transformation::Type::PROCESS_CSS:
        /* processor responses cannot be cached */
        embed->resource_tag = nullptr;

        widget_response_process_css(embed, status, headers, body,
                                    transformation->u.css_processor.options);
        break;

    case Transformation::Type::PROCESS_TEXT:
        /* processor responses cannot be cached */
        embed->resource_tag = nullptr;

        widget_response_process_text(embed, status, headers, body);
        break;

    case Transformation::Type::FILTER:
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
    const Transformation *transformation = embed->transformation;

    if (transformation != nullptr &&
        widget_transformation_enabled(&embed->widget, status)) {
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
                        embed->widget.GetIdPath());
        embed->lookup_handler->error(error, embed->lookup_handler_ctx);
    } else {
        /* no transformation left */

        /* finally pass the response to our handler */
        embed->handler_ref.InvokeResponse(status, headers, body);
    }
}

static void
widget_collect_cookies(struct cookie_jar *jar, const struct strmap *headers,
                       const char *host_and_port)
{
    auto r = headers->EqualRange("set-cookie2");
    if (r.first == r.second)
        r = headers->EqualRange("set-cookie");

    for (auto i = r.first; i != r.second; ++i)
        cookie_jar_set_cookie2(jar, i->value, host_and_port, nullptr);
}

static bool
widget_update_view(struct embed *embed, struct strmap *headers,
                   GError **error_r)
{
    struct widget &widget = embed->widget;

    const char *view_name = headers->Get("x-cm4all-view");
    if (view_name != nullptr) {
        /* yes, look it up in the class */

        const WidgetView *view =
            widget_class_view_lookup(widget.cls, view_name);
        if (view == nullptr) {
            /* the view specified in the response header does not
               exist, bail out */

            daemon_log(4, "No such view: %s\n", view_name);
            g_set_error(error_r, widget_quark(), WIDGET_ERROR_NO_SUCH_VIEW,
                        "No such view: %s", view_name);
            return false;
        }

        /* install the new view */
        embed->transformation = view->transformation;
    } else if (widget.from_request.unauthorized_view &&
               processable(headers) &&
               !widget_is_container(&widget)) {
        /* postponed check from proxy_widget_continue(): an
           unauthorized view was selected, which is only allowed if
           the output is not processable; if it is, we may expose
           internal widget parameters */

        g_set_error(error_r, widget_quark(), WIDGET_ERROR_FORBIDDEN,
                    "view '%s' of widget class '%s' cannot be requested "
                    "because the response is processable",
                    widget_get_transformation_view(&widget)->name,
                    widget.class_name);
        return false;
    }

    return true;
}

static void
widget_response_response(http_status_t status, struct strmap *headers,
                         struct istream *body, void *ctx)
{
    struct embed *embed = (struct embed *)ctx;
    struct widget &widget = embed->widget;

    if (headers != nullptr) {
        if (widget.cls->dump_headers) {
            daemon_log(4, "response headers from widget '%s'\n",
                       widget.GetIdPath());

            for (const auto &i : *headers)
                daemon_log(4, "  %s: %s\n", i.key, i.value);
        }

        if (embed->host_and_port != nullptr) {
            auto *session = session_get(embed->env.session_id);
            if (session != nullptr) {
                widget_collect_cookies(session->cookies, headers,
                                       embed->host_and_port);
                session_put(session);
            }
        }

        /*
        const char *translate = headers->Get("x-cm4all-beng-translate");
        if (translate != nullptr) {
            auto *session = session_get(embed->env.session_id);
            if (session != nullptr)
                session->translate = d_strdup(session->pool, translate);
            session_put(session);
        }
        */

        if (http_status_is_redirect(status)) {
            const char *location = headers->Get("location");
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

    if (embed->content_type != nullptr) {
        headers = headers != nullptr
            ? strmap_dup(&embed->pool, headers)
            : strmap_new(&embed->pool);
        headers->Set("content-type", embed->content_type);
    }

    if (widget.session_save_pending &&
        embed->transformation->HasProcessor()) {
        auto *session = session_get(embed->env.session_id);
        if (session != nullptr) {
            widget_save_session(&widget, session);
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

void
embed::SendRequest()
{
    const WidgetView *a_view = widget_get_address_view(&widget);
    assert(a_view != nullptr);

    const WidgetView *t_view = widget_get_transformation_view(&widget);
    assert(t_view != nullptr);

    host_and_port = widget.cls->cookie_host != nullptr
        ? widget.cls->cookie_host
        : resource_address_host_and_port(&a_view->address);
    transformation = t_view->transformation;

    const auto *address = widget_address(&widget);
    resource_tag = resource_address_id(address, &pool);

    struct istream *request_body = widget.from_request.body;
    widget.from_request.body = nullptr;

    auto *headers =
        widget_request_headers(this, a_view,
                               address->type == RESOURCE_ADDRESS_HTTP ||
                               address->type == RESOURCE_ADDRESS_LHTTP,
                               request_body != nullptr);

    if (widget.cls->dump_headers) {
        daemon_log(4, "request headers for widget '%s'\n",
                   widget.GetIdPath());

        for (const auto &i : *headers)
            daemon_log(4, "  %s: %s\n", i.key, i.value);
    }

    resource_get(global_http_cache, global_tcp_balancer,
                 global_lhttp_stock,
                 global_fcgi_stock, global_was_stock, global_delegate_stock,
                 global_nfs_cache,
                 &pool, session_id_low(env.session_id),
                 widget.from_request.method,
                 address,
                 HTTP_STATUS_OK, headers,
                 request_body,
                 &widget_response_handler, this, &async_ref);
}

static void
widget_suffix_registry_success(const char *content_type,
                               // TODO: apply transformations
                               gcc_unused const Transformation *transformations,
                               void *ctx)
{
    struct embed &embed = *(struct embed *)ctx;

    embed.content_type = content_type;
    embed.SendRequest();
}

static void
widget_suffix_registry_error(GError *error, void *ctx)
{
    struct embed &embed = *(struct embed *)ctx;

    widget_cancel(&embed.widget);
    widget_dispatch_error(&embed, error);
}

static constexpr SuffixRegistryHandler widget_suffix_registry_handler = {
    .success = widget_suffix_registry_success,
    .error = widget_suffix_registry_error,
};

bool
embed::ContentTypeLookup()
{
    return suffix_registry_lookup(pool, *global_translate_cache,
                                  *widget_address(&widget),
                                  widget_suffix_registry_handler, this,
                                  async_ref);
}

/*
 * constructor
 *
 */

void
widget_http_request(struct pool &pool, struct widget &widget,
                    struct processor_env &env,
                    const struct http_response_handler &handler,
                    void *handler_ctx,
                    struct async_operation_ref &async_ref)
{
    assert(widget.cls != nullptr);

    auto embed = NewFromPool<struct embed>(pool, pool, widget, env,
                                           handler, handler_ctx, async_ref);


    if (!embed->ContentTypeLookup())
        embed->SendRequest();
}

void
widget_http_lookup(struct pool &pool, struct widget &widget, const char *id,
                   struct processor_env &env,
                   const struct widget_lookup_handler &handler,
                   void *handler_ctx,
                   struct async_operation_ref &async_ref)
{
    assert(widget.cls != nullptr);
    assert(id != nullptr);
    assert(handler.found != nullptr);
    assert(handler.not_found != nullptr);
    assert(handler.error != nullptr);

    auto embed = NewFromPool<struct embed>(pool, pool, widget, env, id,
                                           handler, handler_ctx, async_ref);

    if (!embed->ContentTypeLookup())
        embed->SendRequest();
}
