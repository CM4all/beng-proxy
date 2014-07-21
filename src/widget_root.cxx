/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget.hxx"
#include "widget_class.hxx"

void
widget_init_root(struct widget *widget, struct pool *pool, const char *id)
{
    widget_init(widget, pool, &root_widget_class);
    widget->id = id;
    widget->lazy.path = "";
    widget->lazy.prefix = "C_";
}
