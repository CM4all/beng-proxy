/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Widget.hxx"
#include "Class.hxx"

Widget::Widget(RootTag, struct pool &_pool, const char *_id)
    :Widget(_pool, &root_widget_class)
{
    id = _id;
    id_path = "";
    prefix = "C_";
}
