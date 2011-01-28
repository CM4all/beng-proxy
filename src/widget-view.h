/*
 * Widget views.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_VIEW_H
#define BENG_PROXY_WIDGET_VIEW_H

#include "resource-address.h"
#include "header-forward.h"

struct pool;

struct widget_view {
    struct widget_view *next;

    /**
     * The name of this view; always NULL for the first (default)
     * view.
     */
    const char *name;

    /** the base URI of this widget, as specified in the template */
    struct resource_address address;

    /**
     * Filter client error messages?
     */
    bool filter_4xx;

    struct transformation *transformation;

    /**
     * Which request headers are forwarded?
     */
    struct header_forward_settings request_header_forward;

    /**
     * Which response headers are forwarded?
     */
    struct header_forward_settings response_header_forward;
};

void
widget_view_init(struct widget_view *view);

bool
widget_view_inherit_address(pool_t pool, struct widget_view *view,
                            const struct resource_address *address);

bool
widget_view_inherit_from(pool_t pool, struct widget_view *dest,
                         const struct widget_view *src);

/**
 * Finds a view by its name.  If name==NULL, it returns the first
 * view.
 */
const struct widget_view *
widget_view_lookup(const struct widget_view *view, const char *name);

struct widget_view *
widget_view_dup_chain(struct pool *pool, const struct widget_view *src);

#endif
