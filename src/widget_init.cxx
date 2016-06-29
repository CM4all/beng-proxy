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
    display = Widget::Display::INLINE;
    from_template.path_info = "";
    from_template.query_string = nullptr;
    from_template.headers = nullptr;
    from_template.view_name = nullptr;
    if (_cls != nullptr)
        from_template.view = &_cls->views;
    approval = Widget::Approval::GIVEN;
    session_scope = Widget::SessionScope::RESOURCE;
    session_sync_pending = false;
    session_save_pending = false;
    from_request.focus_ref = nullptr;
    from_request.path_info = nullptr;
    from_request.query_string = nullptr;
    from_request.method = HTTP_METHOD_GET;
    from_request.body = nullptr;
    if (_cls != nullptr)
        from_request.view = from_template.view;
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
