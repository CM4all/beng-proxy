/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.hxx"
#include "widget_class.hxx"

Widget::Widget(RootTag, struct pool &_pool, const char *_id)
    :Widget(_pool, &root_widget_class)
{
    id = _id;
    id_path = "";
    prefix = "C_";
}
