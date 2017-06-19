/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Widget.hxx"
#include "Class.hxx"

Widget::Widget(struct pool &_pool,
               const WidgetClass *_cls)
    :pool(_pool), cls(_cls)
{
    if (_cls != nullptr)
        from_template.view = from_request.view = &_cls->views;
}
