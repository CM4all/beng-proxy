/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_CLASS_HXX
#define BENG_PROXY_WIDGET_CLASS_HXX

#include "widget_view.hxx"
#include "strset.h"

struct WidgetView;

/**
 * A widget class is a server which provides a widget.
 */
struct widget_class {
    /**
     * A linked list of view descriptions.
     */
    WidgetView views;

    /**
     * The URI prefix that represents '@/'.
     */
    const char *local_uri;

    /**
     * The (beng-proxy) hostname on which requests to this widget are
     * allowed.  If not set, then this is a trusted widget.  Requests
     * from an untrusted widget to a trusted one are forbidden.
     */
    const char *untrusted_host;

    /**
     * The (beng-proxy) hostname prefix on which requests to this
     * widget are allowed.  If not set, then this is a trusted widget.
     * Requests from an untrusted widget to a trusted one are
     * forbidden.
     */
    const char *untrusted_prefix;

    /**
     * A hostname suffix on which requests to this widget are allowed.
     * If not set, then this is a trusted widget.  Requests from an
     * untrusted widget to a trusted one are forbidden.
     */
    const char *untrusted_site_suffix;

    const char *cookie_host;

    /**
     * The group name from #TRANSLATE_WIDGET_GROUP.  It is used to
     * determine whether this widget may be embedded inside another
     * one, see #TRANSLATE_GROUP_CONTAINER and #container_groups.
     */
    const char *group;

    /**
     * If this list is non-empty, then this widget may only embed
     * widgets from any of the specified groups.  The
     * #TRANSLATE_SELF_CONTAINER flag adds an exception to this rule.
     */
    struct strset container_groups;

    /**
     * Does this widget support new-style direct URI addressing?
     *
     * Example: http://localhost/template.html;frame=foo/bar - this
     * requests the widget "foo" and with path-info "/bar".
     */
    bool direct_addressing;

    /** does beng-proxy remember the state (path_info and
        query_string) of this widget? */
    bool stateful;

    /**
     * Absolute URI paths are considered relative to the base URI of
     * the widget.
     */
    bool anchor_absolute;

    /**
     * Send the "info" request headers to the widget?  See
     * #TRANSLATE_WIDGET_INFO.
     */
    bool info_headers;

    bool dump_headers;
};

extern const struct widget_class root_widget_class;

static inline const WidgetView *
widget_class_view_lookup(const struct widget_class *cls, const char *name)
{
    return widget_view_lookup(&cls->views, name);
}

static inline bool
widget_class_has_groups(const struct widget_class *cls)
{
    return !strset_is_empty(&cls->container_groups);
}

static inline bool
widget_class_may_embed(const struct widget_class *container,
                       const struct widget_class *child)
{
    return strset_is_empty(&container->container_groups) ||
        (child->group != NULL && strset_contains(&container->container_groups,
                                                 child->group));
}

#endif
