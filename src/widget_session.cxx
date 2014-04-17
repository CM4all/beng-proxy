/*
 * Widget sessions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "session.h"
#include "tpool.h"
#include "http_address.h"

#include <assert.h>

struct widget_session *
widget_get_session(struct widget *widget, struct session *session,
                   bool create)
{
    struct widget_session *parent, *ws;
    struct pool_mark_state mark;

    assert(widget != NULL);
    assert(session != NULL);
    assert(lock_is_locked(&session->lock));

    if (widget->id == NULL)
        return NULL;

    if (widget->parent == NULL)
        return session_get_widget(session, widget->id, create);

    switch (widget->session) {
    case widget::WIDGET_SESSION_RESOURCE:
        /* the session is bound to the resource: determine
           widget_session from the parent's session */

        parent = widget_get_session(widget->parent, session, create);
        if (parent == NULL)
            return NULL;

        pool_mark(tpool, &mark);
        ws = widget_session_get_child(parent, widget->id, create);
        pool_rewind(tpool, &mark);
        return ws;

    case widget::WIDGET_SESSION_SITE:
        /* this is a site-global widget: get the widget_session
           directly from the session struct (which is site
           specific) */

        pool_mark(tpool, &mark);
        ws = session_get_widget(session, widget->id, create);
        pool_rewind(tpool, &mark);
        return ws;
    }

    assert(0);
    return NULL;
}
