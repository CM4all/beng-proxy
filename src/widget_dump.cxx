/*
 * Dumping widget information to the log file.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_dump.hxx"
#include "widget.hxx"
#include "istream/istream_notify.hxx"

#include <daemon/log.h>

static void dump_widget_tree(unsigned indent, const Widget *widget)
{
    daemon_log(4, "%*swidget id='%s' class='%s'\n", indent, "",
               widget->id, widget->class_name);

    for (auto &child : widget->children)
        dump_widget_tree(indent + 2, &child);
}

static void
widget_dump_callback(void *ctx)
{
    const auto *widget = (const Widget *)ctx;

    dump_widget_tree(0, widget);
}

static constexpr struct istream_notify_handler widget_dump_handler = {
    .eof = widget_dump_callback,
    .abort = widget_dump_callback,
    .close = widget_dump_callback,
};

Istream *
widget_dump_tree_after_istream(struct pool &pool, Istream &istream,
                               Widget &widget)
{
    return istream_notify_new(pool, istream,
                              widget_dump_handler, &widget);
}
