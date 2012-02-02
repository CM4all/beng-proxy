/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_CLASS_H
#define BENG_PROXY_WIDGET_CLASS_H

#include "widget-view.h"

/**
 * A widget class is a server which provides a widget.
 */
struct widget_class {
    /**
     * A linked list of view descriptions.
     */
    struct widget_view views;

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

bool
widget_class_is_container(const struct widget_class *class,
                          const char *view_name);

#endif
