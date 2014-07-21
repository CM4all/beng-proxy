/*
 * Widget views.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_VIEW_HXX
#define BENG_PROXY_WIDGET_VIEW_HXX

#include "resource_address.hxx"
#include "header_forward.hxx"
#include "glibfwd.hxx"

#include <inline/compiler.h>

struct pool;

struct WidgetView {
    WidgetView *next;

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

    void Init();

    /**
     * Copy the specified address into the view, if it does not have an
     * address yet.
     *
     * @return true if the address was inherited, false if the view
     * already had an address or if the specified address is empty
     */
    bool InheritAddress(struct pool &pool,
                        const struct resource_address &src);


    /**
     * Inherit the address and other related settings from one view to
     * another.
     *
     * @return true if attributes were inherited, false if the destination
     * view already had an address or if the source view's address is
     * empty
     */
    bool InheritFrom(struct pool &pool, const WidgetView &src);

    /**
     * Does the effective view enable the HTML processor?
     */
    gcc_pure
    bool HasProcessor() const;

    /**
     * Is this view a container?
     */
    gcc_pure
    bool IsContainer() const;

    /**
     * Does this view need to be expanded with widget_view_expand()?
     */
    gcc_pure
    bool IsExpandable() const;

    /**
     * Expand the strings in this view (not following the linked list)
     * with the specified regex result.
     */
    bool Expand(struct pool &pool, const GMatchInfo &match_info,
                GError **error_r);
};

/**
 * Finds a view by its name.  If name==NULL, it returns the first
 * view.
 */
gcc_pure
const WidgetView *
widget_view_lookup(const WidgetView *view, const char *name);

gcc_malloc
WidgetView *
widget_view_dup_chain(struct pool *pool, const WidgetView *src);

/**
 * Does any view in the linked list need to be expanded with
 * widget_view_expand()?
 */
gcc_pure
bool
widget_view_any_is_expandable(const WidgetView *view);

/**
 * The same as widget_view_expand(), but expand all voews in
 * the linked list.
 */
bool
widget_view_expand_all(struct pool *pool, WidgetView *view,
                       const GMatchInfo *match_info, GError **error_r);

#endif
