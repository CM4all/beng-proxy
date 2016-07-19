/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "frame.hxx"
#include "widget_http.hxx"
#include "widget-quark.h"
#include "penv.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "widget_approval.hxx"
#include "widget_resolver.hxx"
#include "widget_lookup.hxx"
#include "http_response.hxx"
#include "istream/istream.hxx"
#include "session.hxx"

#include <assert.h>

void
frame_top_widget(struct pool *pool, Widget *widget,
                 struct processor_env *env,
                 HttpResponseHandler &handler,
                 CancellablePointer &cancel_ptr)
{
    assert(widget != nullptr);
    assert(widget->cls != nullptr);
    assert(widget->HasDefaultView());
    assert(widget->from_request.frame);
    assert(env != nullptr);

    if (!widget_check_approval(widget)) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_FORBIDDEN,
                        "widget '%s' is not allowed to embed widget '%s'",
                        widget->parent->GetLogName(),
                        widget->GetLogName());
        widget->Cancel();
        handler.InvokeError(error);
        return;
    }

    if (!widget->CheckHost(env->untrusted_host, env->site_name)) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_FORBIDDEN,
                        "untrusted host name mismatch");
        widget->Cancel();
        handler.InvokeError(error);
        return;
    }

    if (widget->session_sync_pending) {
        auto session = env->GetRealmSession();
        if (session)
            widget->LoadFromSession(*session);
        else
            widget->session_sync_pending = false;
    }

    widget_http_request(*pool, *widget, *env,
                        handler, cancel_ptr);
}

void
frame_parent_widget(struct pool *pool, Widget *widget, const char *id,
                    struct processor_env *env,
                    WidgetLookupHandler &handler,
                    CancellablePointer &cancel_ptr)
{
    assert(widget != nullptr);
    assert(widget->cls != nullptr);
    assert(widget->HasDefaultView());
    assert(!widget->from_request.frame);
    assert(id != nullptr);
    assert(env != nullptr);

    if (!widget->IsContainer()) {
        /* this widget cannot possibly be the parent of a framed
           widget if it is not a container */

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_NOT_A_CONTAINER,
                        "frame within non-container requested");
        widget->Cancel();
        handler.WidgetLookupError(error);
        return;
    }

    if (!widget_check_approval(widget)) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_FORBIDDEN,
                        "widget '%s' is not allowed to embed widget '%s'",
                        widget->parent->GetLogName(),
                        widget->GetLogName());
        widget->Cancel();
        handler.WidgetLookupError(error);
        return;
    }

    if (widget->session_sync_pending) {
        auto session = env->GetRealmSession();
        if (session)
            widget->LoadFromSession(*session);
        else
            widget->session_sync_pending = false;
    }

    widget_http_lookup(*pool, *widget, id, *env,
                       handler, cancel_ptr);
}
