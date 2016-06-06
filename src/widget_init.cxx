/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.hxx"
#include "widget_class.hxx"

void
Widget::Init(struct pool &_pool,
             const WidgetClass *_cls)
{
    parent = nullptr;
    pool = &_pool;

    class_name = nullptr;
    cls = _cls;
    resolver = nullptr;
    id = nullptr;
    display = Widget::WIDGET_DISPLAY_INLINE;
    path_info = "";
    query_string = nullptr;
    headers = nullptr;
    view_name = nullptr;
    if (_cls != nullptr)
        view = &_cls->views;
    approval = Widget::WIDGET_APPROVAL_GIVEN;
    session = Widget::WIDGET_SESSION_RESOURCE;
    session_sync_pending = false;
    session_save_pending = false;
    from_request.focus_ref = nullptr;
    from_request.path_info = nullptr;
    from_request.query_string = nullptr;
    from_request.method = HTTP_METHOD_GET;
    from_request.body = nullptr;
    if (_cls != nullptr)
        from_request.view = view;
    from_request.frame = false;
    from_request.unauthorized_view = false;
    for_focused.body = nullptr;
    lazy.path = nullptr;
    lazy.prefix = nullptr;
    lazy.quoted_class_name = nullptr;
    lazy.log_name = nullptr;
    lazy.address = nullptr;
    lazy.stateless_address = nullptr;
}
