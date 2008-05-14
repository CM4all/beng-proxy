/*
 * Widget sessions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "session.h"
#include "tpool.h"
#include "uri-address.h"

#include <assert.h>

static struct session *
widget_get_session2(struct widget *widget)
{
    struct widget_session *ws = widget_get_session(widget_root(widget), false);
    return ws == NULL ? NULL : ws->session;
}

struct widget_session *
widget_get_session(struct widget *widget, bool create)
{
    struct widget_session *parent;
    struct session *session;
    struct pool_mark mark;

    assert(widget != NULL);

    if (widget->from_request.session != NULL ||
        widget->parent == NULL || widget->id == NULL)
        return widget->from_request.session;

    switch (widget->session) {
    case WIDGET_SESSION_RESOURCE:
        /* the session is bound to the resource: determine
           widget_session from the parent's session */

        parent = widget_get_session(widget->parent, create);
        if (parent == NULL)
            return NULL;

        pool_mark(tpool, &mark);
        widget->from_request.session
            = widget_session_get_child(parent, widget->id, create);
        pool_rewind(tpool, &mark);
        return widget->from_request.session;

    case WIDGET_SESSION_SITE:
        /* this is a site-global widget: get the widget_session
           directly from the session struct (which is site
           specific) */

        session = widget_get_session2(widget);
        if (session == NULL)
            return NULL;

        pool_mark(tpool, &mark);
        widget->from_request.session
            = session_get_widget(session, widget->id, create);
        pool_rewind(tpool, &mark);
        return widget->from_request.session;
    }

    assert(0);
    return NULL;
}
