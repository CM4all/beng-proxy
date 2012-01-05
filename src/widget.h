/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_WIDGET_H
#define __BENG_WIDGET_H

#include "strref.h"
#include "resource-address.h"

#include <inline/list.h>
#include <inline/compiler.h>
#include <http/method.h>

#include <stdbool.h>

struct pool;
struct strmap;
struct processor_env;
struct parsed_uri;
struct session;

/**
 * A widget instance.
 */
struct widget {
    struct list_head siblings, children;
    struct widget *parent;

    struct pool *pool;

    const char *class_name;

    /**
     * The widget class.  May be NULL if the #class_name hasn't been
     * looked up yet.
     */
    const struct widget_class *class;

    /**
     * The object that is currently requesting the widget class from
     * the translation server.
     */
    struct widget_resolver *resolver;

    /** the widget's instance id, as specified in the template */
    const char *id;

    /** in which form should this widget be displayed? */
    enum {
        WIDGET_DISPLAY_INLINE,
        WIDGET_DISPLAY_NONE,
    } display;

    /** the path info as specified in the template */
    const char *path_info;

    /** the query string as specified in the template */
    const char *query_string;

    /** HTTP request headers specified in the template */
    struct strmap *headers;

    /** the name of the view specified in the template */
    const char *view;

    /** what is the scope of session data? */
    enum {
        /** each resource has its own set of widget sessions */
        WIDGET_SESSION_RESOURCE,

        /** all resources on this site share the same widget sessions */
        WIDGET_SESSION_SITE,
    } session;

    /**
     * Parameters that were forwarded from the HTTP request to this
     * widget.
     */
    struct {
        /**
         * A reference to the focused widget relative to this one.
         * NULL when the focused widget is not an (indirect) child of
         * this one.
         */
        const struct widget_ref *focus_ref;

        /** the path_info provided by the browser (from processor_env.args) */
        const char *path_info;

        /** the query string provided by the browser (from
            processor_env.external_uri.query_string) */
        struct strref query_string;

        /**
         * The request's HTTP method if the widget is focused.  Falls
         * back to HTTP_METHOD_GET if the widget is not focused.
         */
        http_method_t method;

        /** the request body (from processor_env.body) */
        struct istream * body;

        /** the name of the view requested by the client */
        const char *view;
    } from_request;

    /**
     * Cached attributes that will be initialized lazily.
     */
    struct {
        const char *path;

        const char *prefix;

        const char *quoted_class_name;

        /** the address which is actually retrieved - this is the same
            as class->address, except when the user clicked on a
            relative link */
        const struct resource_address *address;

        /**
         * The widget address including path_info and the query string
         * from the template.  See widget_stateless_address().
         */
        const struct resource_address *stateless_address;
    } lazy;
};

/** a reference to a widget inside a widget.  NULL means the current
    (root) widget is being referenced */
struct widget_ref {
    const struct widget_ref *next;

    const char *id;
};

#define WIDGET_REF_SEPARATOR ':'
#define WIDGET_REF_SEPARATOR_S ":"

void
widget_init(struct widget *widget, struct pool *pool,
            const struct widget_class *class);

void
widget_init_root(struct widget *widget, struct pool *pool, const char *id);

void
widget_set_id(struct widget *widget, struct pool *pool, const struct strref *id);

gcc_pure
static inline struct widget *
widget_root(struct widget *widget)
{
    while (widget->parent != NULL)
        widget = widget->parent;
    return widget;
}

gcc_pure
struct widget *
widget_get_child(struct widget *widget, const char *id);

static inline const char *
widget_path(const struct widget *widget)
{
    return widget->lazy.path;
}

static inline const char *
widget_prefix(const struct widget *widget)
{
    return widget->lazy.prefix;
}

gcc_pure
const char *
widget_get_quoted_class_name(struct widget *widget);

static inline const char *
widget_get_path_info(const struct widget *widget)
{
    return widget->from_request.path_info != NULL
        ? widget->from_request.path_info
        : widget->path_info;
}

/**
 * Returns the effective view name, as specified in the template or
 * requested by the client.
 */
static inline const char *
widget_get_view_name(const struct widget *widget)
{
    return widget->from_request.view != NULL
        ? widget->from_request.view
        : widget->view;
}

gcc_pure
const struct widget_view *
widget_get_view(const struct widget *widget);

/**
 * Returns the widget's session object.  The passed session object
 * must be locked.
 */
struct widget_session *
widget_get_session(struct widget *widget, struct session *session,
                   bool create);

gcc_pure gcc_malloc
const struct widget_ref *
widget_ref_parse(struct pool *pool, const char *p);

/**
 * Is the specified "inner" reference inside or the same as "outer"?
 */
gcc_pure
bool
widget_ref_includes(const struct widget_ref *outer,
                    const struct widget_ref *inner);

const struct resource_address *
widget_determine_address(const struct widget *widget, bool stateful);

gcc_pure
static inline const struct resource_address *
widget_address(struct widget *widget)
{
    if (widget->lazy.address == NULL)
        widget->lazy.address = widget_determine_address(widget, true);

    return widget->lazy.address;
}

gcc_pure
static inline const struct resource_address *
widget_stateless_address(struct widget *widget)
{
    if (widget->lazy.stateless_address == NULL)
        widget->lazy.stateless_address =
            widget_determine_address(widget, false);

    return widget->lazy.stateless_address;
}

gcc_pure
const char *
widget_absolute_uri(struct pool *pool, struct widget *widget, bool stateful,
                    const struct strref *relative_uri);

/**
 * Returns an URI relative to the widget base address.
 */
gcc_pure
const struct strref *
widget_relative_uri(struct pool *pool, struct widget *widget, bool stateful,
                    const char *relative_uri, size_t relative_uri_length,
                    struct strref *buffer);

gcc_pure
const char *
widget_external_uri(struct pool *pool,
                    const struct parsed_uri *external_uri,
                    struct strmap *args,
                    struct widget *widget, bool stateful,
                    const struct strref *relative_uri,
                    const char *frame, const char *view);

/**
 * Determines whether it is allowed to embed the widget in a page with
 * the specified host name.
 */
gcc_pure
bool
widget_check_host(const struct widget *widget, const char *host,
                  const char *site_name);

/**
 * Recursion detection: check if the widget or its parent chain
 * contains the specified class name.
 */
gcc_pure
bool
widget_check_recursion(const struct widget *widget);

/**
 * Free important resources associated with the widget.  A widget
 * callback must call this function on a widget which it will not
 * send a HTTP request to.
 */
void
widget_cancel(struct widget *widget);

#endif
