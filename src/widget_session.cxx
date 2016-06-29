/*
 * Widget sessions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.hxx"
#include "session.hxx"
#include "tpool.hxx"

#include <assert.h>

WidgetSession *
widget_get_session(Widget *widget, RealmSession *session,
                   bool create)
{
    assert(widget != NULL);
    assert(session != NULL);

    if (widget->id == NULL)
        return NULL;

    if (widget->parent == NULL)
        return session->GetWidget(widget->id, create);

    switch (widget->session_scope) {
    case Widget::SessionScope::RESOURCE:
        /* the session is bound to the resource: determine
           widget_session from the parent's session */

        {
            WidgetSession *parent =
                widget_get_session(widget->parent, session, create);
            if (parent == nullptr)
                return nullptr;

            const AutoRewindPool auto_rewind(*tpool);
            return parent->GetChild(widget->id, create);
        }

    case Widget::SessionScope::SITE:
        /* this is a site-global widget: get the widget_session
           directly from the session struct (which is site
           specific) */

        {
            const AutoRewindPool auto_rewind(*tpool);
            return session->GetWidget(widget->id, create);
        }
    }

    assert(0);
    return NULL;
}
