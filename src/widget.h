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
    struct widget *parent;

    const struct widget_class *class;

    /** the widget's instance id, as specified in the template */
    const char *id;

    const char *append_uri;

    /** the URI which is actually retrieved - this is the same as
        base_uri, except when the user clicked on a relative link */
    const char *real_uri;

    /** dimensions of the widget */
    const char *width, *height;

    /** in which form should this widget be displayed? */
    enum {
        WIDGET_DISPLAY_INLINE,
        WIDGET_DISPLAY_IFRAME,
        WIDGET_DISPLAY_IMG,
    } display;

    /** is this the single widget in this whole request which should
        be proxied? */
    unsigned proxy:1;

    /** is this a dock where other widgets of this page may be
        placed? */
    unsigned dock:1;
};


const struct widget_class *
get_widget_class(pool_t pool, const char *uri);

int
widget_class_includes_uri(const struct widget_class *class, const char *uri);


static inline void
widget_init(struct widget *widget, const struct widget_class *class)
{
    list_init(&widget->children);
    widget->parent = NULL;

    widget->class = class;
    widget->id = NULL;
    widget->append_uri = NULL;
    widget->real_uri = NULL;
    widget->width = NULL;
    widget->height = NULL;
    widget->display = WIDGET_DISPLAY_INLINE;
    widget->proxy = 0;
    widget->dock = 0;
}

static inline struct widget *
widget_root(struct widget *widget)
{
    while (widget->parent != NULL)
        widget = widget->parent;
    return widget;
}

static inline const char *
widget_path(const struct widget *widget)
{
    /* XXX */
    return widget->id == NULL ? "_" : widget->id;
}

#endif
