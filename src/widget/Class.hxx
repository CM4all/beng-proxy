/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef BENG_PROXY_WIDGET_CLASS_HXX
#define BENG_PROXY_WIDGET_CLASS_HXX

#include "View.hxx"
#include "util/StringSet.hxx"

/**
 * A widget class is a server which provides a widget.
 */
struct WidgetClass {
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

    /**
     * @see @TRANSLATE_UNTRUSTED_RAW_SITE_SUFFIX
     */
    const char *untrusted_raw_site_suffix;

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
    StringSet container_groups;

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

    WidgetClass() = default;

    struct Root {};
    WidgetClass(Root)
        :views(nullptr),
         local_uri(nullptr),
         untrusted_host(nullptr),
         untrusted_prefix(nullptr),
         untrusted_site_suffix(nullptr),
         untrusted_raw_site_suffix(nullptr),
         cookie_host(nullptr),
         container_groups(),
         direct_addressing(false),
         stateful(false),
         anchor_absolute(false),
         info_headers(false), dump_headers(false) {}

    void Init() {
        views.Init(nullptr);
        local_uri = nullptr;
        untrusted_host = nullptr;
        untrusted_prefix = nullptr;
        untrusted_site_suffix = nullptr;
        untrusted_raw_site_suffix = nullptr;
        cookie_host = nullptr;
        group = nullptr;
        direct_addressing = false;
        stateful = false;
        anchor_absolute = false;
        info_headers = false;
        dump_headers = false;
    }

    /**
     * Determines whether it is allowed to embed the widget in a page with
     * with the specified host name.  If not, throws a
     * std::runtime_error with an explanatory message.
     */
    void CheckHost(const char *host, const char *site_name) const;
};

extern const WidgetClass root_widget_class;

static inline const WidgetView *
widget_class_view_lookup(const WidgetClass *cls, const char *name)
{
    return widget_view_lookup(&cls->views, name);
}

static inline bool
widget_class_has_groups(const WidgetClass *cls)
{
    return !cls->container_groups.IsEmpty();
}

static inline bool
widget_class_may_embed(const WidgetClass *container,
                       const WidgetClass *child)
{
    return container->container_groups.IsEmpty() ||
        (child->group != NULL &&
         container->container_groups.Contains(child->group));
}

#endif
