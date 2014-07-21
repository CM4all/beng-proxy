/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.hxx"
#include "widget_class.hxx"

void
widget::InitRoot(struct pool &_pool, const char *_id)
{
    Init(_pool, &root_widget_class);
    id = _id;
    lazy.path = "";
    lazy.prefix = "C_";
}
