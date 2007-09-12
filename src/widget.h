/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_WIDGET_H
#define __BENG_WIDGET_H

#include "pool.h"
#include "list.h"

struct widget_class {
    /** the base URI of this widget, as specified in the template */
    const char *uri;
};

struct widget {
    struct list_head siblings, children;

    const struct widget_class *class;

    /** the widget's instance id, as specified in the template */
    const char *id;

    /** the URI which is actually retrieved - this is the same as
        base_uri, except when the user clicked on a relative link */
    const char *real_uri;
};


const struct widget_class *
get_widget_class(pool_t pool, const char *uri);

int
widget_class_includes_uri(const struct widget_class *class, const char *uri);


static inline void
widget_init(struct widget *widget, const struct widget_class *class)
{
    list_init(&widget->children);

    widget->class = class;
    widget->id = NULL;
    widget->real_uri = NULL;
}

#endif
