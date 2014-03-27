/*
 * Handle proxying of widget contents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "proxy_widget.hxx"
#include "widget_http.hxx"
#include "widget-lookup.h"
#include "widget_resolver.hxx"
#include "widget.h"
#include "frame.hxx"
#include "request.hxx"
#include "header-writer.h"
#include "header-forward.h"
#include "http_server.h"
#include "http_util.h"
#include "http_response.h"
#include "processor.h"
#include "global.h"
#include "istream-impl.h"
#include "istream.h"
#include "tvary.hxx"

#include <daemon/log.h>

struct proxy_widget {
    struct request *request;

    /**
     * The widget currently being processed.
     */
    struct widget *widget;

    /**
     * A reference to the widget that should be proxied.
     */
    const struct widget_ref *ref;

    struct async_operation operation;
    struct async_operation_ref async_ref;
};

/*
 * http_response_handler
 *
 */

static void
widget_proxy_response(http_status_t status, struct strmap *headers,
                      struct istream *body, void *ctx)
{
    struct proxy_widget *proxy = (struct proxy_widget *)ctx;
    struct request *request2 = proxy->request;
    struct http_server_request *request = request2->request;
    struct widget *widget = proxy->widget;
    struct growing_buffer *headers2;

    assert(widget != nullptr);
    assert(widget->cls != nullptr);

    /* XXX shall the address view or the transformation view be used
       to control response header forwarding? */
    const struct widget_view *view = widget_get_transformation_view(widget);
    assert(view != nullptr);

    headers = forward_response_headers(request->pool, headers,
                                       request->local_host_and_port,
                                       &view->response_header_forward);

    headers = add_translation_vary_header(request->pool, headers,
                                          request2->translate.response);

    request2->product_token = strmap_remove(headers, "server");

#ifdef NO_DATE_HEADER
    request2->date = strmap_remove(headers, "date");
#endif

    headers2 = headers_dup(request->pool, headers);
    if (request->method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers_copy_one(headers, headers2, "content-length");

#ifndef NO_DEFLATE
    if (body != nullptr && istream_available(body, false) == (off_t)-1 &&
        (headers == nullptr || strmap_get(headers, "content-encoding") == nullptr) &&
        http_client_accepts_encoding(request->headers, "deflate")) {
        header_write(headers2, "content-encoding", "deflate");
        body = istream_deflate_new(request->pool, body);
    } else
#endif
#ifdef SPLICE
    if (body != nullptr)
        body = istream_pipe_new(request->pool, body, global_pipe_stock);
#else
    {}
#endif

    /* disable the following transformations, because they are meant
       for the template, not for this widget */
    request2->translate.transformation = nullptr;

    response_dispatch(request2, status, headers2, body);
}

static void
widget_proxy_abort(GError *error, void *ctx)
{
    struct proxy_widget *proxy = (struct proxy_widget *)ctx;
    struct request *request2 = proxy->request;
    struct widget *widget = proxy->widget;

    daemon_log(2, "error from widget on %s: %s\n",
               request2->request->uri, error->message);

    if (widget->for_focused.body != nullptr)
        istream_free_unused(&widget->for_focused.body);

    response_dispatch_error(request2, error);

    g_error_free(error);
}

static const struct http_response_handler widget_response_handler = {
    .response = widget_proxy_response,
    .abort = widget_proxy_abort,
};

/*
 * widget_lookup_handler
 *
 */

extern const struct widget_lookup_handler widget_processor_handler;

/**
 * Is the client allow to select the specified view?
 */
gcc_pure
static bool
widget_view_allowed(struct widget *widget,
                    const struct widget_view *view)
{
    assert(widget != nullptr);
    assert(view != nullptr);
    assert(view->name != nullptr);

    if (widget->view_name != nullptr &&
        strcmp(view->name, widget->view_name) == 0)
        /* always allow when it's the same view that was specified in
           the template */
        return true;

    /* views with an address must not be selected by the client */
    if (!view->inherited) {
        daemon_log(2, "view '%s' of widget class '%s' is forbidden because it has an address\n",
                   view->name, widget->class_name);
        return false;
    }

    /* if the default view is a container, we must await the widget's
       response to see if we allow the new view; if the response is
       processable, it may potentially contain widget elements with
       parameters that must not be exposed to the client */
    if (widget_is_container_by_default(widget))
        /* schedule a check in widget_update_view() */
        widget->from_request.unauthorized_view = true;

    return true;
}

static void
proxy_widget_continue(struct proxy_widget *proxy, struct widget *widget)
{
    struct request *request2 = proxy->request;
    struct http_server_request *request = request2->request;

    if (!widget_has_default_view(widget)) {
        widget_cancel(widget);
        response_dispatch_message(request2, HTTP_STATUS_NOT_FOUND,
                                  "No such view");
        return;
    }

    if (proxy->ref != nullptr) {
        frame_parent_widget(request->pool, widget,
                            proxy->ref->id,
                            &request2->env,
                            &widget_processor_handler, proxy,
                            &proxy->async_ref);
    } else {
        const struct processor_env *env = &request2->env;

        if (env->view_name != nullptr) {
            /* the client can select the view; he can never explicitly
               select the default view */
            const struct widget_view *view =
                widget_class_view_lookup(widget->cls, env->view_name);
            if (view == nullptr || view->name == nullptr) {
                widget_cancel(widget);
                response_dispatch_message(request2, HTTP_STATUS_NOT_FOUND,
                                          "No such view");
                return;
            }

            if (!widget_view_allowed(widget, view)) {
                widget_cancel(widget);
                response_dispatch_message(request2, HTTP_STATUS_FORBIDDEN,
                                          "Forbidden");
                return;
            }

            widget->from_request.view = view;
        }

        if (widget->cls->direct_addressing &&
            !strref_is_empty(&request2->uri.path_info))
            /* apply new-style path_info to frame top widget (direct
               addressing) */
            widget->from_request.path_info =
                p_strndup(request->pool, request2->uri.path_info.data + 1,
                          request2->uri.path_info.length - 1);

        frame_top_widget(request->pool, widget,
                         &request2->env,
                         &widget_response_handler, proxy,
                         &proxy->async_ref);
    }
}

static void
proxy_widget_resolver_callback(void *ctx)
{
    struct proxy_widget *proxy = (struct proxy_widget *)ctx;
    struct request *request2 = proxy->request;
    struct widget *widget = proxy->widget;

    if (widget->cls == nullptr) {
        daemon_log(2, "lookup of widget class '%s' for '%s' failed\n",
                   widget->class_name, widget_path(widget));

        widget_cancel(widget);
        response_dispatch_message(request2, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "No such widget type");
        return;
    }

    proxy_widget_continue(proxy, widget);
}

static void
widget_proxy_found(struct widget *widget, void *ctx)
{
    struct proxy_widget *proxy = (struct proxy_widget *)ctx;
    struct request *request2 = proxy->request;
    struct http_server_request *request = request2->request;

    proxy->widget = widget;
    proxy->ref = proxy->ref->next;

    if (widget->cls == nullptr) {
        widget_resolver_new(request->pool, request2->env.pool, widget,
                            global_translate_cache,
                            &proxy_widget_resolver_callback, proxy,
                            &proxy->async_ref);
        return;
    }

    proxy_widget_continue(proxy, widget);
}

static void
widget_proxy_not_found(void *ctx)
{
    struct proxy_widget *proxy = (struct proxy_widget *)ctx;
    struct request *request2 = proxy->request;
    struct widget *widget = proxy->widget;

    assert(proxy->ref != nullptr);

    daemon_log(2, "widget '%s' not found in %s [%s]\n",
               proxy->ref->id,
               widget_path(widget), request2->request->uri);

    widget_cancel(widget);
    response_dispatch_message(request2, HTTP_STATUS_NOT_FOUND,
                              "No such widget");
}

static void
widget_proxy_error(GError *error, void *ctx)
{
    struct proxy_widget *proxy = (struct proxy_widget *)ctx;
    struct request *request2 = proxy->request;
    struct widget *widget = proxy->widget;

    daemon_log(2, "error from widget on %s: %s\n",
               request2->request->uri, error->message);

    widget_cancel(widget);
    response_dispatch_error(request2, error);

    g_error_free(error);
}

const struct widget_lookup_handler widget_processor_handler = {
    .found = widget_proxy_found,
    .not_found = widget_proxy_not_found,
    .error = widget_proxy_error,
};

/*
 * async operation
 *
 */

static struct proxy_widget *
async_to_proxy(struct async_operation *ao)
{
    void *p = (char *)ao - offsetof(struct proxy_widget, operation);
    return (struct proxy_widget *)p;
}

static void
widget_proxy_operation_abort(struct async_operation *ao)
{
    struct proxy_widget *proxy = async_to_proxy(ao);

    /* make sure that all widget resources are freed when the request
       is cancelled */
    widget_cancel(proxy->widget);

    async_abort(&proxy->async_ref);
}

static const struct async_operation_class widget_proxy_operation = {
    .abort = widget_proxy_operation_abort,
};

/*
 * constructor
 *
 */

void
proxy_widget(struct request *request2,
             struct istream *body,
             struct widget *widget, const struct widget_ref *proxy_ref,
             unsigned options)
{
    assert(request2 != nullptr);
    assert(widget != nullptr);
    assert(proxy_ref != nullptr);

    auto proxy = NewFromPool<struct proxy_widget>(request2->request->pool);
    proxy->request = request2;
    proxy->widget = widget;
    proxy->ref = proxy_ref;

    async_init(&proxy->operation, &widget_proxy_operation);
    async_ref_set(&request2->async_ref, &proxy->operation);

    processor_lookup_widget(request2->request->pool, body,
                            widget, proxy_ref->id,
                            &request2->env, options,
                            &widget_processor_handler, proxy,
                            &proxy->async_ref);
}
