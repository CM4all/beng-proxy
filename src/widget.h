/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_WIDGET_H
#define __BENG_WIDGET_H

#include "pool.h"
#include "strref.h"
#include "http.h"
#include "istream.h"
#include "resource-address.h"

#include <inline/list.h>

#include <assert.h>
#include <stdbool.h>

struct strmap;
struct processor_env;
struct parsed_uri;
struct session;

/**
 * A widget class is a server which provides a widget.
 */
struct widget_class {
    /** the base URI of this widget, as specified in the template */
    struct resource_address address;

    /**
     * The (beng-proxy) hostname on which requests to this widget are
     * allowed.  If not set, then this is a trusted widget.  Requests
     * from an untrusted widget to a trusted one are forbidden.
     */
    const char *host;

    /** transformations applied to the widget response */
    const struct transformation_view *views;

    /** does beng-proxy remember the state (path_info and
        query_string) of this widget? */
    bool stateful;
};

/**
 * A widget instance.
 */
struct widget {
    struct list_head siblings, children;
    struct widget *parent;

    pool_t pool;

    const char *class_name;

    const struct widget_class *class;

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

    struct {
        const struct widget_ref *proxy_ref;

        const struct widget_ref *focus_ref;

        /** the path_info provided by the browser (from processor_env.args) */
        const char *path_info;

        /** the query string provided by the browser (from
            processor_env.external_uri.query_string) */
        struct strref query_string;

        http_method_t method;

        /** the request body (from processor_env.body) */
        istream_t body;

        /** is this the single widget in this whole request which should
            be proxied? */
        bool proxy;

        /** should the resource be passed raw, i.e. not processed? */
        bool raw;

        /** the name of the view requested by the client */
        const char *view;
    } from_request;

    struct {
        const char *path;

        const char *prefix;

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


extern const struct widget_class root_widget_class;

bool
widget_class_is_container(const struct widget_class *class,
                          const char *view_name);

static inline void
widget_init(struct widget *widget, pool_t pool,
            const struct widget_class *class)
{
    list_init(&widget->children);
    widget->parent = NULL;
    widget->pool = pool;

    widget->class_name = NULL;
    widget->class = class;
    widget->resolver = NULL;
    widget->id = NULL;
    widget->display = WIDGET_DISPLAY_INLINE;
    widget->path_info = "";
    widget->query_string = NULL;
    widget->headers = NULL;
    widget->view = NULL;
    widget->session = WIDGET_SESSION_RESOURCE;
    widget->from_request.proxy_ref = NULL;
    widget->from_request.focus_ref = NULL;
    widget->from_request.path_info = NULL;
    strref_clear(&widget->from_request.query_string);
    widget->from_request.method = HTTP_METHOD_GET;
    widget->from_request.body = NULL;
    widget->from_request.proxy = false;
    widget->from_request.raw = false;
    widget->from_request.view = NULL;
    widget->lazy.path = NULL;
    widget->lazy.prefix = NULL;
    widget->lazy.address = NULL;
    widget->lazy.stateless_address = NULL;
}

void
widget_set_id(struct widget *widget, pool_t pool, const struct strref *id);

static inline struct widget *
widget_root(struct widget *widget)
{
    while (widget->parent != NULL)
        widget = widget->parent;
    return widget;
}

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

/**
 * Returns the widget's session object.  The passed session object
 * must be locked.
 */
struct widget_session *
widget_get_session(struct widget *widget, struct session *session,
                   bool create);

const struct widget_ref *
widget_ref_parse(pool_t pool, const char *p);

/**
 * Copy parameters from the request to the widget.
 */
void
widget_copy_from_request(struct widget *widget, struct processor_env *env);

/**
 * Synchronize the widget with its session.
 */
void
widget_sync_session(struct widget *widget, struct session *session);

/**
 * Overwrite request data, copy values from a HTTP redirect location.
 */
void
widget_copy_from_location(struct widget *widget, struct session *session,
                          const char *location, size_t location_length,
                          pool_t pool);

const struct resource_address *
widget_determine_address(const struct widget *widget, bool stateful);

static inline const struct resource_address *
widget_address(struct widget *widget)
{
    if (widget->lazy.address == NULL)
        widget->lazy.address = widget_determine_address(widget, true);

    return widget->lazy.address;
}

static inline const struct resource_address *
widget_stateless_address(struct widget *widget)
{
    if (widget->lazy.stateless_address == NULL)
        widget->lazy.stateless_address =
            widget_determine_address(widget, false);

    return widget->lazy.stateless_address;
}

const char *
widget_absolute_uri(pool_t pool, struct widget *widget, bool stateful,
                    const struct strref *relative_uri);

const char *
widget_translation_uri(pool_t pool,
                       const struct parsed_uri *external_uri,
                       struct strmap *args,
                       const char *translation);

/**
 * Returns an URI relative to the widget base address.
 */
const struct strref *
widget_relative_uri(pool_t pool, struct widget *widget, bool stateful,
                    const char *relative_uri, size_t relative_uri_length,
                    struct strref *buffer);

const char *
widget_external_uri(pool_t pool,
                    const struct parsed_uri *external_uri,
                    struct strmap *args,
                    struct widget *widget, bool stateful,
                    const struct strref *relative_uri,
                    const char *frame, bool raw);

/**
 * Recursion detection: check if the widget or its parent chain
 * contains the specified class name.
 */
bool
widget_check_recursion(struct widget *widget);

/**
 * Free important resources associated with the widget.  A widget
 * callback must call this function on a widget which it will not
 * send a HTTP request to.
 */
void
widget_cancel(struct widget *widget);

#endif
