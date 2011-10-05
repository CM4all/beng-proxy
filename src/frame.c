/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "frame.h"
#include "widget-http.h"
#include "penv.h"
#include "widget.h"
#include "widget-class.h"
#include "widget-resolver.h"
#include "widget-lookup.h"
#include "global.h"
#include "http-response.h"
#include "istream.h"

#include <daemon/log.h>

#include <assert.h>

static inline GQuark
widget_quark(void)
{
    return g_quark_from_static_string("widget");
}

void
frame_top_widget(struct pool *pool, struct widget *widget,
                 struct processor_env *env,
                 const struct http_response_handler *handler,
                 void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    assert(widget != NULL);
    assert(widget->class != NULL);
    assert(env != NULL);

    if (!widget_check_host(widget, env->untrusted_host,
                           env->site_name)) {
        daemon_log(4, "untrusted host name mismatch\n");
        widget_cancel(widget);
        http_response_handler_direct_message(handler, handler_ctx,
                                             pool, HTTP_STATUS_FORBIDDEN,
                                             "Forbidden");
        return;
    }

    if (widget->class->stateful) {
        struct session *session = session_get(env->session_id);
        if (session != NULL) {
            widget_sync_session(widget, session);
            session_put(session);
        }
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
    assert(widget->class != NULL);
    assert(id != NULL);
    assert(env != NULL);

    if (!widget_class_is_container(widget->class,
                                   widget_get_view_name(widget))) {
        /* this widget cannot possibly be the parent of a framed
           widget if it is not a container */

        if (env->request_body != NULL)
            istream_free_unused(&env->request_body);

        GError *error =
            g_error_new(widget_quark(), 0,
                        "frame within non-container requested");
        widget_cancel(widget);
        handler->error(error, handler_ctx);
        return;
    }

    if (widget->class->stateful) {
        struct session *session = session_get(env->session_id);
        if (session != NULL) {
            widget_sync_session(widget, session);
            session_put(session);
        }
    }

    if (env->request_body != NULL && widget->from_request.focus_ref == NULL) {
        /* the request body is not consumed yet, but the focus is not
           within the frame: discard the body, because it cannot ever
           be used */
        assert(!istream_has_handler(env->request_body));

        daemon_log(4, "discarding non-framed request body\n");

        istream_free_unused(&env->request_body);
    }

    widget_http_lookup(pool, widget, id, env,
                       handler, handler_ctx,
                       async_ref);
}
