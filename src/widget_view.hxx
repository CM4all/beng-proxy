/*
 * Widget views.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_VIEW_HXX
#define BENG_PROXY_WIDGET_VIEW_HXX

#include "ResourceAddress.hxx"
#include "header_forward.hxx"

#include <inline/compiler.h>

struct pool;
struct Transformation;
class AllocatorPtr;

struct WidgetView {
    WidgetView *next;

    /**
     * The name of this view; always NULL for the first (default)
     * view.
     */
    const char *name;

    /** the base URI of this widget, as specified in the template */
    ResourceAddress address;

    /**
     * Filter client error messages?
     */
    bool filter_4xx;

    /**
     * Was the address inherited from another view?
     */
    bool inherited;

    Transformation *transformation;

    /**
     * Which request headers are forwarded?
     */
    struct header_forward_settings request_header_forward;

    /**
     * Which response headers are forwarded?
     */
    struct header_forward_settings response_header_forward;

    WidgetView() = default;

    explicit constexpr WidgetView(const ResourceAddress &_address)
        :next(nullptr), name(nullptr), address(ShallowCopy(), _address),
         filter_4xx(false), inherited(false),
         transformation(nullptr),
         request_header_forward(), response_header_forward() {}

    void Init(const char *_name);

    void CopyFrom(AllocatorPtr alloc, const WidgetView &src);

    WidgetView *Clone(AllocatorPtr alloc) const;

    void CopyChainFrom(AllocatorPtr alloc, const WidgetView &src);
    WidgetView *CloneChain(AllocatorPtr alloc) const;

    /**
     * Copy the specified address into the view, if it does not have an
     * address yet.
     *
     * @return true if the address was inherited, false if the view
     * already had an address or if the specified address is empty
     */
    bool InheritAddress(AllocatorPtr alloc,
                        const ResourceAddress &src);


    /**
     * Inherit the address and other related settings from one view to
     * another.
     *
     * @return true if attributes were inherited, false if the destination
     * view already had an address or if the source view's address is
     * empty
     */
    bool InheritFrom(AllocatorPtr alloc, const WidgetView &src);

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
     *
     * Throws std::runtime_error on error.
     */
    void Expand(struct pool &pool, const MatchInfo &match_info);
};

/**
 * Finds a view by its name.  If name==NULL, it returns the first
 * view.
 */
gcc_pure
const WidgetView *
widget_view_lookup(const WidgetView *view, const char *name);

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
void
widget_view_expand_all(struct pool *pool, WidgetView *view,
                       const MatchInfo &match_info);

#endif
