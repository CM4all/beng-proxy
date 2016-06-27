/*
 * Handle proxying of widget contents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "proxy_widget.hxx"
#include "widget_http.hxx"
#include "widget_lookup.hxx"
#include "widget_resolver.hxx"
#include "widget.hxx"
#include "frame.hxx"
#include "request.hxx"
#include "header_writer.hxx"
#include "header_forward.hxx"
#include "http_server/Request.hxx"
#include "http_util.hxx"
#include "http_headers.hxx"
#include "http_response.hxx"
#include "processor.hxx"
#include "bp_global.hxx"
#include "istream/istream.hxx"
#include "istream/istream_pipe.hxx"
#include "tvary.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <glib.h>

#include <daemon/log.h>

struct ProxyWidget final : WidgetLookupHandler {
    Request &request;

    /**
     * The widget currently being processed.
     */
    Widget *widget;

    /**
     * A reference to the widget that should be proxied.
     */
    const struct widget_ref *ref;

    struct async_operation operation;
    struct async_operation_ref async_ref;

    ProxyWidget(Request &_request, Widget &_widget,
                const struct widget_ref *_ref)
        :request(_request), widget(&_widget), ref(_ref) {
        operation.Init2<ProxyWidget>();
    }

    void Abort();

    void Continue();

    void ResolverCallback();

    /* virtual methods from class WidgetLookupHandler */
    void WidgetFound(Widget &widget) override;
    void WidgetNotFound() override;
    void WidgetLookupError(GError *error) override;
};

/*
 * http_response_handler
 *
 */

static void
widget_proxy_response(http_status_t status, StringMap *headers,
                      Istream *body, void *ctx)
{
    auto &proxy = *(ProxyWidget *)ctx;
    auto &request2 = proxy.request;
    const auto &request = request2.request;
    auto &widget = *proxy.widget;

    assert(widget.cls != nullptr);

    /* XXX shall the address view or the transformation view be used
       to control response header forwarding? */
    const WidgetView *view = widget.GetTransformationView();
    assert(view != nullptr);

    headers = forward_response_headers(request2.pool, status, headers,
                                       request.local_host_and_port,
                                       request2.session_cookie,
                                       nullptr, nullptr,
                                       view->response_header_forward);

    headers = add_translation_vary_header(&request2.pool, headers,
                                          request2.translate.response);

    request2.product_token = headers->Remove("server");

#ifdef NO_DATE_HEADER
    request2.date = headers->Remove("date");
#endif

    HttpHeaders headers2(*headers);

    if (request.method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers2.MoveToBuffer(request2.pool, "content-length");

#ifdef SPLICE
    if (body != nullptr)
        body = istream_pipe_new(&request2.pool, *body, global_pipe_stock);
#endif

    /* disable the following transformations, because they are meant
       for the template, not for this widget */
    request2.CancelTransformations();

    response_dispatch(request2, status, std::move(headers2), body);
}

static void
widget_proxy_abort(GError *error, void *ctx)
{
    auto &proxy = *(ProxyWidget *)ctx;
    auto &request2 = proxy.request;
    auto &widget = *proxy.widget;

    daemon_log(2, "error from widget on %s: %s\n",
               request2.request.uri, error->message);

    if (widget.for_focused.body != nullptr)
        istream_free_unused(&widget.for_focused.body);

    response_dispatch_error(request2, error);

    g_error_free(error);
}

static const struct http_response_handler widget_response_handler = {
    .response = widget_proxy_response,
    .abort = widget_proxy_abort,
};

/**
 * Is the client allow to select the specified view?
 */
gcc_pure
static bool
widget_view_allowed(Widget &widget, const WidgetView &view)
{
    assert(view.name != nullptr);

    if (widget.view_name != nullptr &&
        strcmp(view.name, widget.view_name) == 0)
        /* always allow when it's the same view that was specified in
           the template */
        return true;

    /* views with an address must not be selected by the client */
    if (!view.inherited) {
        daemon_log(2, "view '%s' of widget class '%s' is forbidden because it has an address\n",
                   view.name, widget.class_name);
        return false;
    }

    /* if the default view is a container, we must await the widget's
       response to see if we allow the new view; if the response is
       processable, it may potentially contain widget elements with
       parameters that must not be exposed to the client */
    if (widget.IsContainerByDefault())
        /* schedule a check in widget_update_view() */
        widget.from_request.unauthorized_view = true;

    return true;
}

void
ProxyWidget::Continue()
{
    assert(!widget->from_request.frame);

    if (!widget->HasDefaultView()) {
        widget_cancel(widget);
        response_dispatch_message(request, HTTP_STATUS_NOT_FOUND,
                                  "No such view");
        return;
    }

    if (ref != nullptr) {
        frame_parent_widget(&request.pool, widget,
                            ref->id,
                            &request.env,
                            *this, &async_ref);
    } else {
        const struct processor_env *env = &request.env;

        if (env->view_name != nullptr) {
            /* the client can select the view; he can never explicitly
               select the default view */
            const WidgetView *view =
                widget_class_view_lookup(widget->cls, env->view_name);
            if (view == nullptr || view->name == nullptr) {
                widget_cancel(widget);
                response_dispatch_message(request, HTTP_STATUS_NOT_FOUND,
                                          "No such view");
                return;
            }

            if (!widget_view_allowed(*widget, *view)) {
                widget_cancel(widget);
                response_dispatch_message(request, HTTP_STATUS_FORBIDDEN,
                                          "Forbidden");
                return;
            }

            widget->from_request.view = view;
        }

        if (widget->cls->direct_addressing &&
            !request.uri.path_info.IsEmpty())
            /* apply new-style path_info to frame top widget (direct
               addressing) */
            widget->from_request.path_info =
                p_strndup(&request.pool, request.uri.path_info.data + 1,
                          request.uri.path_info.size - 1);

        widget->from_request.frame = true;

        frame_top_widget(&request.pool, widget,
                         &request.env,
                         &widget_response_handler, this,
                         &async_ref);
    }
}

void
ProxyWidget::ResolverCallback()
{
    if (widget->cls == nullptr) {
        daemon_log(2, "lookup of widget class for '%s' failed\n",
                   widget->GetLogName());

        widget_cancel(widget);
        response_dispatch_message(request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "No such widget type");
        return;
    }

    Continue();
}

void
ProxyWidget::WidgetFound(Widget &_widget)
{
    assert(ref != nullptr);

    widget = &_widget;
    ref = ref->next;

    if (widget->cls == nullptr) {
        ResolveWidget(request.pool, *widget,
                      *global_translate_cache,
                      BIND_THIS_METHOD(ResolverCallback),
                      async_ref);
        return;
    }

    Continue();
}

void
ProxyWidget::WidgetNotFound()
{
    assert(ref != nullptr);

    daemon_log(2, "widget '%s' not found in %s [%s]\n",
               ref->id, widget->GetLogName(), request.request.uri);

    widget_cancel(widget);
    response_dispatch_message(request, HTTP_STATUS_NOT_FOUND,
                              "No such widget");
}

void
ProxyWidget::WidgetLookupError(GError *error)
{
    daemon_log(2, "error from widget on %s: %s\n",
               request.request.uri, error->message);

    widget_cancel(widget);
    response_dispatch_error(request, error);

    g_error_free(error);
}

/*
 * async operation
 *
 */

void
ProxyWidget::Abort()
{
    /* make sure that all widget resources are freed when the request
       is cancelled */
    widget_cancel(widget);

    async_ref.Abort();
}

/*
 * constructor
 *
 */

void
proxy_widget(Request &request2,
             Istream &body,
             Widget &widget, const struct widget_ref *proxy_ref,
             unsigned options)
{
    assert(!widget.from_request.frame);
    assert(proxy_ref != nullptr);

    auto proxy = NewFromPool<ProxyWidget>(request2.pool, request2,
                                          widget, proxy_ref);

    request2.async_ref.Set(proxy->operation);

    processor_lookup_widget(request2.pool, body,
                            widget, proxy_ref->id,
                            request2.env, options,
                            *proxy, proxy->async_ref);
}
