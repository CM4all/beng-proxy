/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.h"
#include "widget-class.h"

void
widget_init(struct widget *widget, struct pool *pool,
            const struct widget_class *class)
{
    list_init(&widget->children);
    widget->parent = NULL;
    widget->pool = pool;

    widget->class_name = NULL;
    widget->class = class;
    widget->resolver = NULL;
    widget->id = NULL;
    widget->display = WIDGET_DISPLAY_INLINE;
    widget->path_info = "";
    widget->query_string = NULL;
    widget->headers = NULL;
    widget->view_name = NULL;
    if (class != NULL)
        widget->view = &class->views;
    widget->session = WIDGET_SESSION_RESOURCE;
    widget->session_sync_pending = false;
    widget->session_save_pending = false;
    widget->from_request.focus_ref = NULL;
    widget->from_request.path_info = NULL;
    strref_clear(&widget->from_request.query_string);
    widget->from_request.method = HTTP_METHOD_GET;
    widget->from_request.body = NULL;
    if (class != NULL)
        widget->from_request.view = widget->view;
    widget->from_request.unauthorized_view = false;
    widget->for_focused.body = NULL;
    widget->lazy.path = NULL;
    widget->lazy.prefix = NULL;
    widget->lazy.quoted_class_name = NULL;
    widget->lazy.address = NULL;
    widget->lazy.stateless_address = NULL;
}
