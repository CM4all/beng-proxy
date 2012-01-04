/*
 * Handle proxying of widget contents.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "proxy-widget.h"
#include "widget-http.h"
#include "widget-lookup.h"
#include "widget-resolver.h"
#include "widget.h"
#include "frame.h"
#include "request.h"
#include "header-writer.h"
#include "header-forward.h"
#include "http-server.h"
#include "http-util.h"
#include "http-response.h"
#include "processor.h"
#include "global.h"
#include "istream-impl.h"
#include "istream.h"

#include <daemon/log.h>

static void
widget_proxy_response(http_status_t status, struct strmap *headers,
                      struct istream *body, void *ctx)
{
    struct request *request2 = ctx;
    struct http_server_request *request = request2->request;
    struct widget *widget = request2->widget;
    struct growing_buffer *headers2;

    assert(widget != NULL);
    assert(widget->class != NULL);

    const struct widget_view *view = widget_get_view(widget);
    assert(view != NULL);

    headers = forward_response_headers(request->pool, headers,
                                       request->local_host,
                                       &view->response_header_forward);

    request2->product_token = strmap_remove(headers, "server");

    headers2 = headers_dup(request->pool, headers);
    if (request->method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers_copy_one(headers, headers2, "content-length");

#ifndef NO_DEFLATE
    if (body != NULL && istream_available(body, false) == (off_t)-1 &&
        (headers == NULL || strmap_get(headers, "content-encoding") == NULL) &&
        http_client_accepts_encoding(request->headers, "deflate")) {
        header_write(headers2, "content-encoding", "deflate");
        body = istream_deflate_new(request->pool, body);
    } else
#endif
#ifdef SPLICE
    if (body != NULL)
        body = istream_pipe_new(request->pool, body, global_pipe_stock);
#else
    {}
#endif

    /* disable the following transformations, because they are meant
       for the template, not for this widget */
    request2->translate.transformation = NULL;

    response_dispatch(request2, status, headers2, body);
}

static void
widget_proxy_abort(GError *error, void *ctx)
{
    struct request *request2 = ctx;

    daemon_log(2, "error from widget on %s: %s\n",
               request2->request->uri, error->message);

    if (request2->env.request_body != NULL)
        istream_free_unused(&request2->env.request_body);

    response_dispatch_error(request2, error);

    g_error_free(error);
}

static const struct http_response_handler widget_response_handler = {
    .response = widget_proxy_response,
    .abort = widget_proxy_abort,
};

static const struct widget_lookup_handler widget_processor_handler;

static void
proxy_widget_continue(struct request *request2, struct widget *widget)
{
    struct http_server_request *request = request2->request;

    if (request2->proxy_ref != NULL) {
        if (widget_get_view(widget) == NULL) {
            response_dispatch_message(request2, HTTP_STATUS_NOT_FOUND,
                                      "No such view");
            return;
        }

        frame_parent_widget(request->pool, widget,
                            request2->proxy_ref->id,
                            &request2->env,
                            &widget_processor_handler, request2,
                            &request2->async_ref);
    } else {
        const struct processor_env *env = &request2->env;

        /* the client can select the view; he can never explicitly
           select the default view */
        widget->from_request.view = env->view_name;

        const struct widget_view *view = widget_get_view(widget);
        if (view == NULL) {
            response_dispatch_message(request2, HTTP_STATUS_NOT_FOUND,
                                      "No such view");
            return;
        }

        /* the client may choose only views that are not
           "inherited" */
        if (env->view_name != NULL && view->inherited &&
            /* exception from the rule: allow an inherited view when
               it was specified in the template */
            (widget->view == NULL || strcmp(env->view_name,
                                            widget->view) != 0)) {
            daemon_log(2, "view '%s' of widget class '%s' cannot be requested "
                       "because it does not have an address\n",
                       env->view_name, widget->class_name);
            response_dispatch_message(request2, HTTP_STATUS_FORBIDDEN,
                                      "Forbidden");
            return;
        }

        frame_top_widget(request->pool, widget,
                         &request2->env,
                         &widget_response_handler, request2,
                         &request2->async_ref);
    }
}

static void
proxy_widget_resolver_callback(void *ctx)
{
    struct request *request2 = ctx;
    struct widget *widget = request2->widget;

    if (widget->class == NULL) {
        daemon_log(2, "lookup of widget class '%s' for '%s' failed\n",
                   widget->class_name, widget_path(widget));

        widget_cancel(widget);
        response_dispatch_message(request2, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "No such widget type");
        return;
    }

    proxy_widget_continue(request2, widget);
}

static void
widget_proxy_found(struct widget *widget, void *ctx)
{
    struct request *request2 = ctx;
    struct http_server_request *request = request2->request;

    request2->widget = widget;
    request2->proxy_ref = request2->proxy_ref->next;

    if (widget->class == NULL) {
        widget_resolver_new(request->pool, request2->env.pool, widget,
                            global_translate_cache,
                            &proxy_widget_resolver_callback, request2,
                            &request2->async_ref);
        return;
    }

    proxy_widget_continue(request2, widget);
}

static void
widget_proxy_not_found(void *ctx)
{
    struct request *request2 = ctx;
    struct widget *widget = request2->widget;

    assert(request2->proxy_ref != NULL);

    daemon_log(2, "widget '%s' not found in %s [%s]\n",
               request2->proxy_ref->id,
               widget_path(widget), request2->request->uri);

    if (request2->env.request_body != NULL)
        istream_free_unused(&request2->env.request_body);

    widget_cancel(widget);
    response_dispatch_message(request2, HTTP_STATUS_NOT_FOUND,
                              "No such widget");
}

static void
widget_proxy_error(GError *error, void *ctx)
{
    struct request *request2 = ctx;
    struct widget *widget = request2->widget;

    daemon_log(2, "error from widget on %s: %s\n",
               request2->request->uri, error->message);

    if (request2->env.request_body != NULL)
        istream_free_unused(&request2->env.request_body);

    widget_cancel(widget);
    response_dispatch_error(request2, error);

    g_error_free(error);
}

static const struct widget_lookup_handler widget_processor_handler = {
    .found = widget_proxy_found,
    .not_found = widget_proxy_not_found,
    .error = widget_proxy_error,
};

void
proxy_widget(struct request *request2, http_status_t status,
             struct istream *body,
             struct widget *widget, const struct widget_ref *proxy_ref,
             unsigned options)
{
    assert(request2 != NULL);
    assert(widget != NULL);
    assert(proxy_ref != NULL);

    request2->widget = widget;
    request2->proxy_ref = proxy_ref;

    processor_lookup_widget(request2->request->pool, status, body,
                            widget, proxy_ref->id,
                            &request2->env, options,
                            &widget_processor_handler, request2,
                            &request2->async_ref);
}
