/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "frame.h"
#include "widget-http.h"
#include "widget-quark.h"
#include "penv.h"
#include "widget.h"
#include "widget-class.h"
#include "widget-approval.h"
#include "widget-resolver.h"
#include "widget-lookup.h"
#include "widget-request.h"
#include "global.h"
#include "http_response.h"
#include "istream.h"

#include <assert.h>

void
frame_top_widget(struct pool *pool, struct widget *widget,
                 struct processor_env *env,
                 const struct http_response_handler *handler,
                 void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    assert(widget != NULL);
    assert(widget->cls != NULL);
    assert(widget_has_default_view(widget));
    assert(env != NULL);

    if (!widget_check_approval(widget)) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_FORBIDDEN,
                        "widget '%s'[class='%s'] is not allowed to embed widget class '%s'",
                        widget_path(widget->parent),
                        widget->parent->class_name,
                        widget->class_name);
        widget_cancel(widget);
        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    if (!widget_check_host(widget, env->untrusted_host,
                           env->site_name)) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_FORBIDDEN,
                        "untrusted host name mismatch");
        widget_cancel(widget);
        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    if (widget->session_sync_pending) {
        struct session *session = session_get(env->session_id);
        if (session != NULL) {
            widget_sync_session(widget, session);
            session_put(session);
        } else
            widget->session_sync_pending = false;
    }

    widget_http_request(pool, widget, env,
                        handler, handler_ctx, async_ref);
}

void
frame_parent_widget(struct pool *pool, struct widget *widget, const char *id,
                    struct processor_env *env,
                    const struct widget_lookup_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    assert(widget != NULL);
    assert(widget->cls != NULL);
    assert(widget_has_default_view(widget));
    assert(id != NULL);
    assert(env != NULL);

    if (!widget_is_container(widget)) {
        /* this widget cannot possibly be the parent of a framed
           widget if it is not a container */

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_NOT_A_CONTAINER,
                        "frame within non-container requested");
        widget_cancel(widget);
        handler->error(error, handler_ctx);
        return;
    }

    if (!widget_check_approval(widget)) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_FORBIDDEN,
                        "widget '%s'[class='%s'] is not allowed to embed widget class '%s'",
                        widget_path(widget->parent),
                        widget->parent->class_name,
                        widget->class_name);
        widget_cancel(widget);
        handler->error(error, handler_ctx);
        return;
    }

    if (widget->session_sync_pending) {
        struct session *session = session_get(env->session_id);
        if (session != NULL) {
            widget_sync_session(widget, session);
            session_put(session);
        } else
            widget->session_sync_pending = false;
    }

    widget_http_lookup(pool, widget, id, env,
                       handler, handler_ctx,
                       async_ref);
}
