/*
 * Send HTTP requests to a widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_http.hxx"
#include "http_response.hxx"
#include "pheaders.hxx"
#include "processor.hxx"
#include "css_processor.hxx"
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
#include "ResourceLoader.hxx"
#include "header_writer.hxx"
#include "header_forward.hxx"
#include "transformation.hxx"
#include "bp_global.hxx"
#include "resource_tag.hxx"
#include "uri/uri_extract.hxx"
#include "strmap.hxx"
#include "istream/istream.hxx"
#include "istream/istream_pipe.hxx"
#include "pool.hxx"
#include "suffix_registry.hxx"
#include "address_suffix_registry.hxx"
#include "util/Cast.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

struct WidgetRequest {
    struct pool &pool;

    unsigned num_redirects = 0;

    Widget &widget;
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

    WidgetLookupHandler *lookup_handler;

    struct http_response_handler_ref handler_ref;
    struct async_operation operation;
    struct async_operation_ref async_ref;

    WidgetRequest(struct pool &_pool, Widget &_widget,
                  struct processor_env &_env,
                  const struct http_response_handler &_handler,
                  void *_handler_ctx,
                  struct async_operation_ref &_async_ref)
        :pool(_pool), widget(_widget), env(_env) {
        handler_ref.Set(_handler, _handler_ctx);
        operation.Init2<WidgetRequest>();
        _async_ref.Set(operation);
    }

    WidgetRequest(struct pool &_pool, Widget &_widget,
                  struct processor_env &_env,
                  const char *_lookup_id,
                  WidgetLookupHandler &_handler,
                  struct async_operation_ref &_async_ref)
        :pool(_pool), widget(_widget),
         lookup_id(_lookup_id),
         env(_env),
         lookup_handler(&_handler) {
        operation.Init2<WidgetRequest>();
        _async_ref.Set(operation);
    }

    RealmSessionLease GetSessionIfStateful() const {
        return widget.cls->stateful
            ? env.GetRealmSession()
            : nullptr;
    }

    /**
     * @param a_view the view that is used to determine the address
     * @param t_view the view that is used to determine the transformations
     */
    StringMap *MakeRequestHeaders(const WidgetView &a_view,
                                  const WidgetView &t_view,
                                  bool exclude_host, bool with_body);

    bool HandleRedirect(const char *location, Istream *body);

    void DispatchError(GError *error);

    /**
     * A response was received from the widget server; apply
     * transformations (if enabled) and return it to our handler.
     * This function will be called (semi-)recursively for every
     * transformation in the chain.
     */
    void DispatchResponse(http_status_t status, StringMap *headers,
                          Istream *body);

    /**
     * The widget response is going to be embedded into a template; check
     * its content type and run the processor (if applicable).
     */
    void ProcessResponse(http_status_t status,
                         StringMap *headers, Istream *body,
                         unsigned options);

    void CssProcessResponse(http_status_t status,
                            StringMap *headers, Istream *body,
                            unsigned options);

    void TextProcessResponse(http_status_t status,
                             StringMap *headers, Istream *body);

    void FilterResponse(http_status_t status,
                        StringMap *headers, Istream *body,
                        const ResourceAddress &filter, bool reveal_user);

    /**
     * Apply a transformation to the widget response and hand it back
     * to widget_response_handler.
     */
    void TransformResponse(http_status_t status,
                           StringMap *headers, Istream *body,
                           const Transformation &t);

    bool UpdateView(StringMap &headers, GError **error_r);

    bool ContentTypeLookup();
    void SendRequest();

    void Abort() {
        widget_cancel(&widget);
        async_ref.Abort();
    }
};

static const char *
widget_uri(Widget *widget)
{
    const ResourceAddress *address = widget_address(widget);
    if (address == nullptr)
        return nullptr;

    return address->GetUriPath();
}

StringMap *
WidgetRequest::MakeRequestHeaders(const WidgetView &a_view,
                                  const WidgetView &t_view,
                                  bool exclude_host, bool with_body)
{
    auto *headers =
        forward_request_headers(pool, *env.request_headers,
                                env.local_host,
                                env.remote_host,
                                exclude_host, with_body,
                                widget.from_request.frame && !t_view.HasProcessor(),
                                widget.from_request.frame && t_view.transformation == nullptr,
                                widget.from_request.frame && t_view.transformation == nullptr,
                                a_view.request_header_forward,
                                env.session_cookie,
                                env.GetRealmSession().get(),
                                host_and_port,
                                widget_uri(&widget));

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
            headers->Add(p_strdup(&pool, i.key),
                         p_strdup(&pool, i.value));

    return headers;
}

extern const struct http_response_handler widget_response_handler;

bool
WidgetRequest::HandleRedirect(const char *location, Istream *body)
{
    if (num_redirects >= 8)
        return false;

    const WidgetView *view = widget.GetAddressView();
    assert(view != nullptr);

    if (!view->address.IsHttp())
        /* a static or CGI widget cannot send redirects */
        return false;

    const auto p = widget_relative_uri(&pool, &widget, true, location);
    if (p.IsNull())
        return false;

    widget_copy_from_location(widget, GetSessionIfStateful().get(),
                              p.data, p.size, pool);

    ++num_redirects;

    ResourceAddress address_buffer;
    const auto *address =
        widget_address(&widget)->Apply(pool, location,
                                       address_buffer);
    if (address == nullptr)
        return false;

    if (body != nullptr)
        body->CloseUnused();

    const WidgetView *t_view = widget.GetTransformationView();
    assert(t_view != nullptr);

    auto *headers = MakeRequestHeaders(*view, *t_view,
                                       address->IsAnyHttp(),
                                       false);

    env.resource_loader->SendRequest(pool, env.session_id.GetClusterHash(),
                                     HTTP_METHOD_GET, *address, HTTP_STATUS_OK,
                                     headers, nullptr, nullptr,
                                     widget_response_handler, this,
                                     async_ref);

    return true;
}

void
WidgetRequest::DispatchError(GError *error)
{
    assert(error != nullptr);

    if (lookup_id != nullptr)
        lookup_handler->WidgetLookupError(error);
    else
        handler_ref.InvokeAbort(error);
}

void
WidgetRequest::ProcessResponse(http_status_t status,
                               StringMap *headers, Istream *body,
                               unsigned options)
{
    if (body == nullptr) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_EMPTY,
                        "widget '%s' didn't send a response body",
                        widget.GetLogName());
        DispatchError(error);
        return;
    }

    if (!processable(headers)) {
        body->CloseUnused();

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                        "widget '%s' sent non-HTML response",
                        widget.GetLogName());
        DispatchError(error);
        return;
    }

    if (lookup_id != nullptr)
        processor_lookup_widget(pool, *body,
                                widget, lookup_id,
                                env, options,
                                *lookup_handler,
                                async_ref);
    else {
        headers = processor_header_forward(&pool, headers);
        body = processor_process(pool, *body,
                                 widget, env, options);

        DispatchResponse(status, headers, body);
    }
}

static bool
css_processable(const StringMap *headers)
{
    const char *content_type = strmap_get_checked(headers, "content-type");
    return content_type != nullptr &&
        strncmp(content_type, "text/css", 8) == 0;
}

void
WidgetRequest::CssProcessResponse(http_status_t status,
                                  StringMap *headers, Istream *body,
                                  unsigned options)
{
    if (body == nullptr) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_EMPTY,
                        "widget '%s' didn't send a response body",
                        widget.GetLogName());
        DispatchError(error);
        return;
    }

    if (!css_processable(headers)) {
        body->CloseUnused();

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                        "widget '%s' sent non-CSS response",
                        widget.GetLogName());
        DispatchError(error);
        return;
    }

    headers = processor_header_forward(&pool, headers);
    body = css_processor(pool, *body, widget, env, options);
    DispatchResponse(status, headers, body);
}

void
WidgetRequest::TextProcessResponse(http_status_t status,
                                   StringMap *headers, Istream *body)
{
    if (body == nullptr) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_EMPTY,
                        "widget '%s' didn't send a response body",
                        widget.GetLogName());
        DispatchError(error);
        return;
    }

    if (!text_processor_allowed(headers)) {
        body->CloseUnused();

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                        "widget '%s' sent non-text response",
                        widget.GetLogName());
        DispatchError(error);
        return;
    }

    headers = processor_header_forward(&pool, headers);
    body = text_processor(pool, *body, widget, env);
    DispatchResponse(status, headers, body);
}

void
WidgetRequest::FilterResponse(http_status_t status,
                              StringMap *headers, Istream *body,
                              const ResourceAddress &filter, bool reveal_user)
{
    const char *source_tag = resource_tag_append_etag(&pool, resource_tag,
                                                      headers);
    resource_tag = source_tag != nullptr
        ? p_strcat(&pool, source_tag, "|", filter.GetId(pool), nullptr)
        : nullptr;

    if (reveal_user)
        headers = forward_reveal_user(pool, headers,
                                      GetSessionIfStateful().get());

#ifdef SPLICE
    if (body != nullptr)
        body = istream_pipe_new(&pool, *body, global_pipe_stock);
#endif

    env.filter_resource_loader
        ->SendRequest(pool, env.session_id.GetClusterHash(),

                      HTTP_METHOD_POST, filter, status,
                      headers, body, source_tag,
                      widget_response_handler, this,
                      async_ref);
}

void
WidgetRequest::TransformResponse(http_status_t status,
                                 StringMap *headers, Istream *body,
                                 const Transformation &t)
{
    assert(transformation == t.next);

    const char *p = strmap_get_checked(headers, "content-encoding");
    if (p != nullptr && strcmp(p, "identity") != 0) {
        if (body != nullptr)
            body->CloseUnused();

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_UNSUPPORTED_ENCODING,
                        "widget '%s' sent non-identity response, "
                        "cannot transform",
                        widget.GetLogName());
        DispatchError(error);
        return;
    }

    switch (t.type) {
    case Transformation::Type::PROCESS:
        /* processor responses cannot be cached */
        resource_tag = nullptr;

        ProcessResponse(status, headers, body, t.u.processor.options);
        break;

    case Transformation::Type::PROCESS_CSS:
        /* processor responses cannot be cached */
        resource_tag = nullptr;

        CssProcessResponse(status, headers, body, t.u.css_processor.options);
        break;

    case Transformation::Type::PROCESS_TEXT:
        /* processor responses cannot be cached */
        resource_tag = nullptr;

        TextProcessResponse(status, headers, body);
        break;

    case Transformation::Type::FILTER:
        FilterResponse(status, headers, body,
                       t.u.filter.address, t.u.filter.reveal_user);
        break;
    }
}

static bool
widget_transformation_enabled(const Widget *widget,
                              http_status_t status)
{
    assert(widget->GetTransformationView() != nullptr);

    return http_status_is_success(status) ||
        (http_status_is_client_error(status) &&
         widget->GetTransformationView()->filter_4xx);
}

void
WidgetRequest::DispatchResponse(http_status_t status, StringMap *headers,
                                Istream *body)
{
    const Transformation *t = transformation;

    if (t != nullptr && widget_transformation_enabled(&widget, status)) {
        /* transform this response */

        transformation = t->next;

        TransformResponse(status, headers, body, *t);
    } else if (lookup_id != nullptr) {
        if (body != nullptr)
            body->CloseUnused();

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_NOT_A_CONTAINER,
                        "Cannot process container widget response of %s",
                        widget.GetLogName());
        lookup_handler->WidgetLookupError(error);
    } else {
        /* no transformation left */

        /* finally pass the response to our handler */
        handler_ref.InvokeResponse(status, headers, body);
    }
}

static void
widget_collect_cookies(CookieJar &jar, const StringMap &headers,
                       const char *host_and_port)
{
    auto r = headers.EqualRange("set-cookie2");
    if (r.first == r.second)
        r = headers.EqualRange("set-cookie");

    for (auto i = r.first; i != r.second; ++i)
        cookie_jar_set_cookie2(jar, i->value, host_and_port, nullptr);
}

bool
WidgetRequest::UpdateView(StringMap &headers, GError **error_r)
{
    const char *view_name = headers.Get("x-cm4all-view");
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
        transformation = view->transformation;
    } else if (widget.from_request.unauthorized_view &&
               processable(&headers) &&
               !widget.IsContainer()) {
        /* postponed check from proxy_widget_continue(): an
           unauthorized view was selected, which is only allowed if
           the output is not processable; if it is, we may expose
           internal widget parameters */

        g_set_error(error_r, widget_quark(), WIDGET_ERROR_FORBIDDEN,
                    "view '%s' of widget class '%s' cannot be requested "
                    "because the response is processable",
                    widget.GetTransformationView()->name,
                    widget.class_name);
        return false;
    }

    return true;
}

static void
widget_response_response(http_status_t status, StringMap *headers,
                         Istream *body, void *ctx)
{
    WidgetRequest *embed = (WidgetRequest *)ctx;
    auto &widget = embed->widget;

    if (headers != nullptr) {
        if (widget.cls->dump_headers) {
            daemon_log(4, "response headers from widget '%s'\n",
                       widget.GetLogName());

            for (const auto &i : *headers)
                daemon_log(4, "  %s: %s\n", i.key, i.value);
        }

        if (embed->host_and_port != nullptr) {
            auto session = embed->env.GetRealmSession();
            if (session)
                widget_collect_cookies(session->cookies, *headers,
                                       embed->host_and_port);
        } else {
#ifndef NDEBUG
            auto r = headers->EqualRange("set-cookie2");
            if (r.first == r.second)
                r = headers->EqualRange("set-cookie");
            if (r.first != r.second)
                daemon_log(4, "ignoring Set-Cookie from widget '%s': no host\n",
                           widget.GetLogName());
#endif
        }

        if (http_status_is_redirect(status)) {
            const char *location = headers->Get("location");
            if (location != nullptr && embed->HandleRedirect(location, body)) {
                return;
            }
        }

        /* select a new view? */

        GError *error = nullptr;
        if (!embed->UpdateView(*headers, &error)) {
            if (body != nullptr)
                body->CloseUnused();

            embed->DispatchError(error);
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
        auto session = embed->env.GetRealmSession();
        if (session)
            widget_save_session(widget, *session);
    }

    embed->DispatchResponse(status, headers, body);
}

static void
widget_response_abort(GError *error, void *ctx)
{
    WidgetRequest *embed = (WidgetRequest *)ctx;

    embed->DispatchError(error);
}

const struct http_response_handler widget_response_handler = {
    .response = widget_response_response,
    .abort = widget_response_abort,
};

void
WidgetRequest::SendRequest()
{
    const WidgetView *a_view = widget.GetAddressView();
    assert(a_view != nullptr);

    const WidgetView *t_view = widget.GetTransformationView();
    assert(t_view != nullptr);

    host_and_port = widget.cls->cookie_host != nullptr
        ? widget.cls->cookie_host
        : a_view->address.GetHostAndPort();
    transformation = t_view->transformation;

    const auto *address = widget_address(&widget);
    resource_tag = address->GetId(pool);

    Istream *request_body = widget.from_request.body;
    widget.from_request.body = nullptr;

    auto *headers = MakeRequestHeaders(*a_view, *t_view,
                                       address->IsAnyHttp(),
                                       request_body != nullptr);

    if (widget.cls->dump_headers) {
        daemon_log(4, "request headers for widget '%s'\n",
                   widget.GetLogName());

        for (const auto &i : *headers)
            daemon_log(4, "  %s: %s\n", i.key, i.value);
    }

    env.resource_loader->SendRequest(pool, env.session_id.GetClusterHash(),
                                     widget.from_request.method,
                                     *address, HTTP_STATUS_OK,
                                     headers,
                                     request_body, nullptr,
                                     widget_response_handler, this, async_ref);
}

static void
widget_suffix_registry_success(const char *content_type,
                               // TODO: apply transformations
                               gcc_unused const Transformation *transformations,
                               void *ctx)
{
    WidgetRequest &embed = *(WidgetRequest *)ctx;

    embed.content_type = content_type;
    embed.SendRequest();
}

static void
widget_suffix_registry_error(GError *error, void *ctx)
{
    WidgetRequest &embed = *(WidgetRequest *)ctx;

    widget_cancel(&embed.widget);
    embed.DispatchError(error);
}

static constexpr SuffixRegistryHandler widget_suffix_registry_handler = {
    .success = widget_suffix_registry_success,
    .error = widget_suffix_registry_error,
};

bool
WidgetRequest::ContentTypeLookup()
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
widget_http_request(struct pool &pool, Widget &widget,
                    struct processor_env &env,
                    const struct http_response_handler &handler,
                    void *handler_ctx,
                    struct async_operation_ref &async_ref)
{
    assert(widget.cls != nullptr);

    auto embed = NewFromPool<WidgetRequest>(pool, pool, widget, env,
                                           handler, handler_ctx, async_ref);


    if (!embed->ContentTypeLookup())
        embed->SendRequest();
}

void
widget_http_lookup(struct pool &pool, Widget &widget, const char *id,
                   struct processor_env &env,
                   WidgetLookupHandler &handler,
                   struct async_operation_ref &async_ref)
{
    assert(widget.cls != nullptr);
    assert(id != nullptr);

    auto embed = NewFromPool<WidgetRequest>(pool, pool, widget, env, id,
                                            handler, async_ref);

    if (!embed->ContentTypeLookup())
        embed->SendRequest();
}
