/*
 * Widget views.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_VIEW_H
#define BENG_PROXY_WIDGET_VIEW_H

struct pool;

struct widget_view {
    struct widget_view *next;

    /**
     * The name of this view; always NULL for the first (default)
     * view.
     */
    const char *name;

    struct transformation *transformation;
};

void
widget_view_init(struct widget_view *view);

/**
 * Finds a view by its name.  If name==NULL, it returns the first
 * view.
 */
const struct widget_view *
widget_view_lookup(const struct widget_view *view, const char *name);

struct widget_view *
widget_view_dup_chain(struct pool *pool, const struct widget_view *src);

#endif
