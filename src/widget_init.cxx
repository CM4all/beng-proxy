/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.hxx"
#include "widget_class.hxx"

void
widget_init(struct widget *widget, struct pool *pool,
            const struct widget_class *cls)
{
    list_init(&widget->children);
    widget->parent = nullptr;
    widget->pool = pool;

    widget->class_name = nullptr;
    widget->cls = cls;
    widget->resolver = nullptr;
    widget->id = nullptr;
    widget->display = widget::WIDGET_DISPLAY_INLINE;
    widget->path_info = "";
    widget->query_string = nullptr;
    widget->headers = nullptr;
    widget->view_name = nullptr;
    if (cls != nullptr)
        widget->view = &cls->views;
    widget->approval = widget::WIDGET_APPROVAL_GIVEN;
    widget->session = widget::WIDGET_SESSION_RESOURCE;
    widget->session_sync_pending = false;
    widget->session_save_pending = false;
    widget->from_request.focus_ref = nullptr;
    widget->from_request.path_info = nullptr;
    strref_clear(&widget->from_request.query_string);
    widget->from_request.method = HTTP_METHOD_GET;
    widget->from_request.body = nullptr;
    if (cls != nullptr)
        widget->from_request.view = widget->view;
    widget->from_request.unauthorized_view = false;
    widget->for_focused.body = nullptr;
    widget->lazy.path = nullptr;
    widget->lazy.prefix = nullptr;
    widget->lazy.quoted_class_name = nullptr;
    widget->lazy.address = nullptr;
    widget->lazy.stateless_address = nullptr;
}
