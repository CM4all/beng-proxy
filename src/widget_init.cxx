/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.hxx"
#include "widget_class.hxx"

void
widget::Init(struct pool &_pool,
             const WidgetClass *_cls)
{
    list_init(&children);
    parent = nullptr;
    pool = &_pool;

    class_name = nullptr;
    cls = _cls;
    resolver = nullptr;
    id = nullptr;
    display = widget::WIDGET_DISPLAY_INLINE;
    path_info = "";
    query_string = nullptr;
    headers = nullptr;
    view_name = nullptr;
    if (_cls != nullptr)
        view = &_cls->views;
    approval = widget::WIDGET_APPROVAL_GIVEN;
    session = widget::WIDGET_SESSION_RESOURCE;
    session_sync_pending = false;
    session_save_pending = false;
    from_request.focus_ref = nullptr;
    from_request.path_info = nullptr;
    strref_clear(&from_request.query_string);
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
    lazy.address = nullptr;
    lazy.stateless_address = nullptr;
}
