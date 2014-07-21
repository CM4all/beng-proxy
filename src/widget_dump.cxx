/*
 * Dumping widget information to the log file.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_dump.hxx"
#include "widget.hxx"
#include "istream-notify.h"

#include <daemon/log.h>

static void dump_widget_tree(unsigned indent, const struct widget *widget)
{
    const struct widget *child;

    daemon_log(4, "%*swidget id='%s' class='%s'\n", indent, "",
               widget->id, widget->class_name);

    for (child = (const struct widget *)widget->children.next;
         &child->siblings != &widget->children;
         child = (const struct widget *)child->siblings.next)
        dump_widget_tree(indent + 2, child);
}

static void
widget_dump_callback(void *ctx)
{
    const struct widget *widget = (const struct widget *)ctx;

    dump_widget_tree(0, widget);
}

static const struct istream_notify_handler widget_dump_handler = {
    .eof = widget_dump_callback,
    .abort = widget_dump_callback,
    .close = widget_dump_callback,
};

struct istream *
widget_dump_tree_after_istream(struct pool *pool, struct istream *istream,
                               struct widget *widget)
{
    assert(widget != nullptr);

    return istream_notify_new(pool, istream,
                              &widget_dump_handler, widget);
}
