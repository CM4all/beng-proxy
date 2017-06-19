/*
 * Send HTTP requests to a widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Request.hxx"
#include "Widget.hxx"
#include "Class.hxx"
#include "Error.hxx"
#include "LookupHandler.hxx"
#include "http_response.hxx"
#include "pheaders.hxx"
#include "processor.hxx"
#include "css_processor.hxx"
#include "text_processor.hxx"
#include "penv.hxx"
#include "session.hxx"
#include "cookie_client.hxx"
#include "ResourceLoader.hxx"
#include "header_writer.hxx"
#include "header_forward.hxx"
#include "translation/Transformation.hxx"
#include "bp_global.hxx"
#include "resource_tag.hxx"
#include "uri/uri_extract.hxx"
#include "strmap.hxx"
#include "istream/istream.hxx"
#include "istream/istream_pipe.hxx"
#include "pool.hxx"
#include "GException.hxx"
#include "suffix_registry.hxx"
#include "address_suffix_registry.hxx"
#include "util/Cast.hxx"
#include "util/Cancellable.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

struct WidgetRequest final : HttpResponseHandler, Cancellable {
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

    HttpResponseHandler *http_handler;
    CancellablePointer cancel_ptr;

    WidgetRequest(struct pool &_pool, Widget &_widget,
                  struct processor_env &_env,
                  HttpResponseHandler &_handler,
                  CancellablePointer &_cancel_ptr)
        :pool(_pool), widget(_widget), env(_env), http_handler(&_handler) {
        _cancel_ptr = *this;
    }

    WidgetRequest(struct pool &_pool, Widget &_widget,
                  struct processor_env &_env,
                  const char *_lookup_id,
                  WidgetLookupHandler &_handler,
                  CancellablePointer &_cancel_ptr)
        :pool(_pool), widget(_widget),
         lookup_id(_lookup_id),
         env(_env),
         lookup_handler(&_handler) {
        _cancel_ptr = *this;
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
    StringMap MakeRequestHeaders(const WidgetView &a_view,
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
    void DispatchResponse(http_status_t status, StringMap &&headers,
                          Istream *body);

    /**
     * The widget response is going to be embedded into a template; check
     * its content type and run the processor (if applicable).
     */
    void ProcessResponse(http_status_t status,
                         StringMap &headers, Istream *body,
                         unsigned options);

    void CssProcessResponse(http_status_t status,
                            StringMap &headers, Istream *body,
                            unsigned options);

    void TextProcessResponse(http_status_t status,
                             StringMap &headers, Istream *body);

    void FilterResponse(http_status_t status,
                        StringMap &&headers, Istream *body,
                        const ResourceAddress &filter, bool reveal_user);

    /**
     * Apply a transformation to the widget response and hand it back
     * to our #HttpResponseHandler implementation.
     */
    void TransformResponse(http_status_t status,
                           StringMap &&headers, Istream *body,
                           const Transformation &t);

    bool UpdateView(StringMap &headers, GError **error_r);

    bool ContentTypeLookup();
    void SendRequest();

    /* virtual methods from class Cancellable */
    void Cancel() override {
        widget.Cancel();
        cancel_ptr.Cancel();
    }

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(GError *error) override;
};

static const char *
widget_uri(Widget *widget)
{
    const auto *address = widget->GetAddress();
    if (address == nullptr)
        return nullptr;

    return address->GetUriPath();
}

StringMap
WidgetRequest::MakeRequestHeaders(const WidgetView &a_view,
                                  const WidgetView &t_view,
                                  bool exclude_host, bool with_body)
{
    auto headers =
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
            headers.Add("x-cm4all-widget-id", widget.id);

        if (widget.class_name != nullptr)
            headers.Add("x-cm4all-widget-type", widget.class_name);

        const char *prefix = widget.GetPrefix();
        if (prefix != nullptr)
            headers.Add("x-cm4all-widget-prefix", prefix);
    }

    if (widget.from_template.headers != nullptr)
        /* copy HTTP request headers from template */
        for (const auto &i : *widget.from_template.headers)
            headers.Add(p_strdup(&pool, i.key),
                         p_strdup(&pool, i.value));

    return headers;
}

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

    const auto p = widget.RelativeUri(pool, true, location);
    if (p.IsNull())
        return false;

    widget.CopyFromRedirectLocation(p, GetSessionIfStateful().get());

    ++num_redirects;

    const auto address = widget.GetAddress()->Apply(pool, location);
    if (!address.IsDefined())
        return false;

    if (body != nullptr)
        body->CloseUnused();

    const WidgetView *t_view = widget.GetTransformationView();
    assert(t_view != nullptr);

    env.resource_loader->SendRequest(pool, env.session_id.GetClusterHash(),
                                     HTTP_METHOD_GET, address, HTTP_STATUS_OK,
                                     MakeRequestHeaders(*view, *t_view,
                                                        address.IsAnyHttp(),
                                                        false),
                                     nullptr, nullptr,
                                     *this,
                                     cancel_ptr);

    return true;
}

void
WidgetRequest::DispatchError(GError *error)
{
    assert(error != nullptr);

    if (lookup_id != nullptr)
        lookup_handler->WidgetLookupError(error);
    else
        http_handler->InvokeError(error);
}

void
WidgetRequest::ProcessResponse(http_status_t status,
                               StringMap &headers, Istream *body,
                               unsigned options)
{
    if (body == nullptr) {
        /* this should not happen, but we're ignoring this formal
           mistake and pretend everything's alright */
        DispatchResponse(status, processor_header_forward(pool, headers),
                         nullptr);
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
                                cancel_ptr);
    else {
        body = processor_process(pool, *body,
                                 widget, env, options);

        DispatchResponse(status, processor_header_forward(pool, headers),
                         body);
    }
}

static bool
css_processable(const StringMap &headers)
{
    const char *content_type = headers.Get("content-type");
    return content_type != nullptr &&
        strncmp(content_type, "text/css", 8) == 0;
}

void
WidgetRequest::CssProcessResponse(http_status_t status,
                                  StringMap &headers, Istream *body,
                                  unsigned options)
{
    if (body == nullptr) {
        /* this should not happen, but we're ignoring this formal
           mistake and pretend everything's alright */
        DispatchResponse(status, processor_header_forward(pool, headers),
                         nullptr);
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

    body = css_processor(pool, *body, widget, env, options);
    DispatchResponse(status, processor_header_forward(pool, headers), body);
}

void
WidgetRequest::TextProcessResponse(http_status_t status,
                                   StringMap &headers, Istream *body)
{
    if (body == nullptr) {
        /* this should not happen, but we're ignoring this formal
           mistake and pretend everything's alright */
        DispatchResponse(status, processor_header_forward(pool, headers),
                         nullptr);
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

    body = text_processor(pool, *body, widget, env);
    DispatchResponse(status, processor_header_forward(pool, headers), body);
}

void
WidgetRequest::FilterResponse(http_status_t status,
                              StringMap &&headers, Istream *body,
                              const ResourceAddress &filter, bool reveal_user)
{
    const char *source_tag = resource_tag_append_etag(&pool, resource_tag,
                                                      headers);
    resource_tag = source_tag != nullptr
        ? p_strcat(&pool, source_tag, "|", filter.GetId(pool), nullptr)
        : nullptr;

    if (reveal_user)
        forward_reveal_user(headers, GetSessionIfStateful().get());

#ifdef SPLICE
    if (body != nullptr)
        body = istream_pipe_new(&pool, *body, global_pipe_stock);
#endif

    env.filter_resource_loader
        ->SendRequest(pool, env.session_id.GetClusterHash(),

                      HTTP_METHOD_POST, filter, status,
                      std::move(headers), body, source_tag,
                      *this,
                      cancel_ptr);
}

void
WidgetRequest::TransformResponse(http_status_t status,
                                 StringMap &&headers, Istream *body,
                                 const Transformation &t)
{
    assert(transformation == t.next);

    const char *p = headers.Get("content-encoding");
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
        FilterResponse(status, std::move(headers), body,
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
WidgetRequest::DispatchResponse(http_status_t status, StringMap &&headers,
                                Istream *body)
{
    const Transformation *t = transformation;

    if (t != nullptr && widget_transformation_enabled(&widget, status)) {
        /* transform this response */

        transformation = t->next;

        TransformResponse(status, std::move(headers), body, *t);
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
        http_handler->InvokeResponse(status, std::move(headers), body);
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
               processable(headers) &&
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

void
WidgetRequest::OnHttpResponse(http_status_t status, StringMap &&headers,
                              Istream *body)
{
    if (widget.cls->dump_headers) {
        daemon_log(4, "response headers from widget '%s'\n",
                   widget.GetLogName());

        for (const auto &i : headers)
            daemon_log(4, "  %s: %s\n", i.key, i.value);
    }

    if (host_and_port != nullptr) {
        auto session = env.GetRealmSession();
        if (session)
            widget_collect_cookies(session->cookies, headers,
                                   host_and_port);
    } else {
#ifndef NDEBUG
        auto r = headers.EqualRange("set-cookie2");
        if (r.first == r.second)
            r = headers.EqualRange("set-cookie");
        if (r.first != r.second)
            daemon_log(4, "ignoring Set-Cookie from widget '%s': no host\n",
                       widget.GetLogName());
#endif
    }

    if (http_status_is_redirect(status)) {
        const char *location = headers.Get("location");
        if (location != nullptr && HandleRedirect(location, body)) {
            return;
        }
    }

    /* select a new view? */

    GError *error = nullptr;
    if (!UpdateView(headers, &error)) {
        if (body != nullptr)
            body->CloseUnused();

        DispatchError(error);
        return;
    }

    if (content_type != nullptr)
        headers.Set("content-type", content_type);

    if (widget.session_save_pending &&
        Transformation::HasProcessor(transformation)) {
        auto session = env.GetRealmSession();
        if (session)
            widget.SaveToSession(*session);
    }

    DispatchResponse(status, std::move(headers), body);
}

void
WidgetRequest::OnHttpError(GError *error)
{
    DispatchError(error);
}

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

    const auto *address = widget.GetAddress();

    if (!address->IsDefined()) {
        const char *view_name = widget.from_template.view_name;
        if (view_name == nullptr)
            view_name = "[default]";

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_UNSPECIFIED,
                        "Widget '%s' view '%s' does not have an address",
                        widget.GetLogName(), view_name);
        DispatchError(error);
        return;
    }

    resource_tag = address->GetId(pool);

    Istream *request_body = widget.from_request.body;
    widget.from_request.body = nullptr;

    auto headers = MakeRequestHeaders(*a_view, *t_view,
                                      address->IsAnyHttp(),
                                      request_body != nullptr);

    if (widget.cls->dump_headers) {
        daemon_log(4, "request headers for widget '%s'\n",
                   widget.GetLogName());

        for (const auto &i : headers)
            daemon_log(4, "  %s: %s\n", i.key, i.value);
    }

    env.resource_loader->SendRequest(pool, env.session_id.GetClusterHash(),
                                     widget.from_request.method,
                                     *address, HTTP_STATUS_OK,
                                     std::move(headers),
                                     request_body, nullptr,
                                     *this, cancel_ptr);
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
widget_suffix_registry_error(std::exception_ptr ep, void *ctx)
{
    WidgetRequest &embed = *(WidgetRequest *)ctx;

    embed.widget.Cancel();
    embed.DispatchError(ToGError(ep));
}

static constexpr SuffixRegistryHandler widget_suffix_registry_handler = {
    .success = widget_suffix_registry_success,
    .error = widget_suffix_registry_error,
};

bool
WidgetRequest::ContentTypeLookup()
{
    return suffix_registry_lookup(pool, *global_translate_cache,
                                  *widget.GetAddress(),
                                  widget_suffix_registry_handler, this,
                                  cancel_ptr);
}

/*
 * constructor
 *
 */

void
widget_http_request(struct pool &pool, Widget &widget,
                    struct processor_env &env,
                    HttpResponseHandler &handler,
                    CancellablePointer &cancel_ptr)
{
    assert(widget.cls != nullptr);

    auto embed = NewFromPool<WidgetRequest>(pool, pool, widget, env,
                                            handler, cancel_ptr);

    if (!embed->ContentTypeLookup())
        embed->SendRequest();
}

void
widget_http_lookup(struct pool &pool, Widget &widget, const char *id,
                   struct processor_env &env,
                   WidgetLookupHandler &handler,
                   CancellablePointer &cancel_ptr)
{
    assert(widget.cls != nullptr);
    assert(id != nullptr);

    auto embed = NewFromPool<WidgetRequest>(pool, pool, widget, env, id,
                                            handler, cancel_ptr);

    if (!embed->ContentTypeLookup())
        embed->SendRequest();
}
