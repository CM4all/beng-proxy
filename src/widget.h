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

    /** dimensions of the widget */
    const char *width, *height;

    /** in which form should this widget be displayed? */
    enum {
        WIDGET_DISPLAY_INLINE,
        WIDGET_DISPLAY_IFRAME,
        WIDGET_DISPLAY_IMG,
    } display;

    /** the path info as specified in the template */
    const char *path_info;

    /** the query string as specified in the template */
    const char *query_string;

    struct {
        /** the path_info provided by the browser (from processor_env.args) */
        const char *path_info;

        struct widget_session *session;

        /** is there a query string being forwarded to the widget
            server? */
        unsigned query_string:1;

        /** is there a request body being forwarded to the widget
            server? */
        unsigned body:1;

        /** is this the single widget in this whole request which should
            be proxied? */
        unsigned proxy:1;
    } from_request;

    /** the URI which is actually retrieved - this is the same as
        base_uri, except when the user clicked on a relative link */
    const char *real_uri;
};

/** a reference to a widget inside a widget.  NULL means the current
    (root) widget is being referenced */
struct widget_ref {
    const struct widget_ref *next;

    const char *id;
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
    widget->width = NULL;
    widget->height = NULL;
    widget->display = WIDGET_DISPLAY_INLINE;
    widget->path_info = NULL;
    widget->query_string = NULL;
    widget->from_request.path_info = NULL;
    widget->from_request.session = NULL;
    widget->from_request.query_string = 0;
    widget->from_request.body = 0;
    widget->from_request.proxy = 0;
    widget->real_uri = NULL;
}

static inline struct widget *
widget_root(struct widget *widget)
{
    while (widget->parent != NULL)
        widget = widget->parent;
    return widget;
}

const char *
widget_path(pool_t pool, const struct widget *widget);

const char *
widget_prefix(pool_t pool, const struct widget *widget);

struct widget_session *
widget_get_session(struct widget *widget, int create);

const struct widget_ref *
widget_ref_parse(pool_t pool, const char *p);

int
widget_ref_compare(pool_t pool, const struct widget *widget,
                   const struct widget_ref *ref, int partial_ok);

#endif
