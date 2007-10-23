/*
 * Widget sessions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "session.h"

#include <assert.h>

struct widget_session *
widget_get_session(struct widget *widget, int create)
{
    struct widget_session *parent;

    assert(widget != NULL);

    if (widget->from_request.session != NULL ||
        widget->parent == NULL || widget->id == NULL)
        return widget->from_request.session;

    parent = widget_get_session(widget->parent, create);
    if (parent == NULL)
        return NULL;

    return widget->from_request.session
        = widget_session_get_child(parent, widget->id, create);
}
