/*
 * Widget views.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_VIEW_H
#define BENG_PROXY_WIDGET_VIEW_H

#include "resource-address.h"
#include "header-forward.h"

#include <inline/compiler.h>

#include <glib.h>

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

    /**
     * Was the address inherited from another view?
     */
    bool inherited;

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

/**
 * Copy the specified address into the view, if it does not have an
 * address yet.
 *
 * @return true if the address was inherited, false if the view
 * already had an address or if the specified address is empty
 */
bool
widget_view_inherit_address(struct pool *pool, struct widget_view *view,
                            const struct resource_address *address);

/**
 * Inherit the address and other related settings from one view to
 * another.
 *
 * @return true if attributes were inherited, false if the destination
 * view already had an address or if the source view's address is
 * empty
 */
bool
widget_view_inherit_from(struct pool *pool, struct widget_view *dest,
                         const struct widget_view *src);

/**
 * Finds a view by its name.  If name==NULL, it returns the first
 * view.
 */
gcc_pure
const struct widget_view *
widget_view_lookup(const struct widget_view *view, const char *name);

/**
 * Does the effective view enable the HTML processor?
 */
gcc_pure
bool
widget_view_has_processor(const struct widget_view *view);

/**
 * Is this view a container?
 */
gcc_pure
bool
widget_view_is_container(const struct widget_view *view);

gcc_malloc
struct widget_view *
widget_view_dup_chain(struct pool *pool, const struct widget_view *src);

/**
 * Does this view need to be expanded with widget_view_expand()?
 */
gcc_pure
bool
widget_view_is_expandable(const struct widget_view *view);

/**
 * Does any view in the linked list need to be expanded with
 * widget_view_expand()?
 */
gcc_pure
bool
widget_view_any_is_expandable(const struct widget_view *view);

/**
 * Expand the strings in this view (not following the linked list)
 * with the specified regex result.
 */
bool
widget_view_expand(struct pool *pool, struct widget_view *view,
                   const GMatchInfo *match_info, GError **error_r);

/**
 * The same as widget_view_expand(), but expand all voews in
 * the linked list.
 */
bool
widget_view_expand_all(struct pool *pool, struct widget_view *view,
                       const GMatchInfo *match_info, GError **error_r);

#endif
