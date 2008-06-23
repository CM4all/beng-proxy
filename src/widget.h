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

enum widget_type {
    WIDGET_TYPE_RAW,
    WIDGET_TYPE_BENG,
    WIDGET_TYPE_GOOGLE_GADGET,
};

/**
 * A widget class is a server which provides a widget.
 */
struct widget_class {
    /** the base URI of this widget, as specified in the template */
    struct resource_address address;

    /** which API is used by the widget */
    enum widget_type type;

    /** can this widget contain other widgets? */
    bool is_container:1;
};

/**
 * A widget instance.
 */
struct widget {
    struct list_head siblings, children;
    struct widget *parent;

    const char *class_name;

    const struct widget_class *class;

    struct widget_resolver *resolver;

    /** the widget's instance id, as specified in the template */
    const char *id;

    struct {
        /** which SGML tag is rendered as container for this widget?  NULL
            means unset, empty string means don't generate anything */
        const char *tag;

        /** custom CSS */
        const char *style;
    } decoration;

    /** in which form should this widget be displayed? */
    enum {
        WIDGET_DISPLAY_INLINE,
        WIDGET_DISPLAY_NONE,
    } display;

    /** the path info as specified in the template */
    const char *path_info;

    /** the query string as specified in the template */
    const char *query_string;

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
        bool proxy:1;

        /** should the resource be passed raw, i.e. not processed? */
        bool raw:1;
    } from_request;

    struct {
        const char *path;

        const char *prefix;

        /** the address which is actually retrieved - this is the same
            as class->address, except when the user clicked on a
            relative link */
        const struct resource_address *address;
    } lazy;
};

/** a reference to a widget inside a widget.  NULL means the current
    (root) widget is being referenced */
struct widget_ref {
    const struct widget_ref *next;

    const char *id;
};


extern const struct widget_class root_widget_class;

const struct strref *
widget_class_relative_uri(const struct widget_class *class,
                          struct strref *uri);


static inline void
widget_init(struct widget *widget, const struct widget_class *class)
{
    list_init(&widget->children);
    widget->parent = NULL;

    widget->class_name = NULL;
    widget->class = class;
    widget->resolver = NULL;
    widget->id = NULL;
    widget->decoration.tag = NULL;
    widget->decoration.style = NULL;
    widget->display = WIDGET_DISPLAY_INLINE;
    widget->path_info = "";
    widget->query_string = NULL;
    widget->session = WIDGET_SESSION_RESOURCE;
    widget->from_request.proxy_ref = NULL;
    widget->from_request.focus_ref = NULL;
    widget->from_request.path_info = NULL;
    strref_clear(&widget->from_request.query_string);
    widget->from_request.method = HTTP_METHOD_GET;
    widget->from_request.body = NULL;
    widget->from_request.proxy = false;
    widget->from_request.raw = false;
    widget->lazy.path = NULL;
    widget->lazy.prefix = NULL;
    widget->lazy.address = NULL;
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

void
widget_determine_address(pool_t pool, struct widget *widget);

static inline const struct resource_address *
widget_address(pool_t pool, struct widget *widget)
{
    if (widget->lazy.address == NULL)
        widget_determine_address(pool, widget);

    return widget->lazy.address;
}

const char *
widget_absolute_uri(pool_t pool, struct widget *widget,
                    const char *relative_uri, size_t relative_uri_length);

const char *
widget_translation_uri(pool_t pool,
                       const struct parsed_uri *external_uri,
                       struct strmap *args,
                       const char *translation);

const char *
widget_external_uri(pool_t pool,
                    const struct parsed_uri *external_uri,
                    struct strmap *args,
                    struct widget *widget,
                    bool focus,
                    const char *relative_uri, size_t relative_uri_length,
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
