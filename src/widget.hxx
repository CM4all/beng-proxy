/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_HXX
#define BENG_PROXY_WIDGET_HXX

#include "strref.h"

#include <inline/list.h>
#include <inline/compiler.h>
#include <http/method.h>

struct pool;
struct strmap;
struct processor_env;
struct parsed_uri;
struct session;
struct WidgetView;

/**
 * A widget instance.
 */
struct widget {
    struct list_head siblings, children;
    struct widget *parent;

    struct pool *pool;

    const char *class_name;

    /**
     * The widget class.  May be nullptr if the #class_name hasn't been
     * looked up yet.
     */
    const struct widget_class *cls;

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
    const char *view_name;

    /**
     * The view that was specified in the template.  This attribute is
     * undefined before the widget resolver finishes.  Being nullptr is a
     * fatal error, and means that no operation is possible on this
     * widget.
     */
    const WidgetView *view;

    /**
     * The approval level for embedding this widget into its
     * container.  This is based on #TRANSLATE_SELF_CONTAINER and
     * #TRANSLATE_GROUP_CONTAINER.
     */
    enum {
        /**
         * Approval was given.
         */
        WIDGET_APPROVAL_GIVEN,

        /**
         * Approval was denied.
         */
        WIDGET_APPROVAL_DENIED,

        /**
         * Approval has not been verified yet.
         */
        WIDGET_APPROVAL_UNKNOWN,
    } approval;

    /** what is the scope of session data? */
    enum {
        /** each resource has its own set of widget sessions */
        WIDGET_SESSION_RESOURCE,

        /** all resources on this site share the same widget sessions */
        WIDGET_SESSION_SITE,
    } session;

    /**
     * This is set to true by the widget resolver when the widget
     * class is "stateful".  It means that widget_sync_session() must
     * be called, which in turn resets the flag.  It protects against
     * calling widget_sync_session() twice.
     */
    bool session_sync_pending;

    /**
     * This is set to true by widget_sync_session(), and is checked by
     * widget_response_response().  The current request will only be
     * saved to the session if the actual response from the widget
     * server is processable.
     */
    bool session_save_pending;

    /**
     * Parameters that were forwarded from the HTTP request to this
     * widget.
     */
    struct {
        /**
         * A reference to the focused widget relative to this one.
         * nullptr when the focused widget is not an (indirect) child of
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

        /**
         * The view requested by the client.  If no view was
         * explicitly requested, then this is the view selected by the
         * template.  This attribute is undefined before the widget
         * resolver finishes.
         */
        const WidgetView *view;

        /**
         * This flag is set when the view selected by the client is
         * unauthorized, and will only be allowed when the widget
         * response is not processable.  If it is, we might expose
         * internal widget parameters by switching off the processor.
         */
        bool unauthorized_view;
    } from_request;

    /**
     * Parameters that will forwarded from the HTTP request to the
     * focused widget (which is an (indirect) child of this widget).
     */
    struct {
        /**
         * The request body.  This must be closed if it failed to be
         * submitted to the focused widget.
         */
        struct istream * body;
    } for_focused;

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

    void Init(struct pool &_pool, const struct widget_class *_cls);
    void InitRoot(struct pool &_pool, const char *_id);

    void SetId(const struct strref &_id);
    void SetClassName(const struct strref &_class_name);

    const char *GetIdPath() const {
        return lazy.path;
    }

    const char *GetPrefix() const {
        return lazy.prefix;
    }

    const char *GetQuotedClassName() const {
        return lazy.quoted_class_name;
    }

    gcc_pure
    struct widget *FindRoot() {
        struct widget *w = this;
        while (w->parent != nullptr)
            w = w->parent;
        return w;
    }

    gcc_pure
    struct widget *FindChild(const char *child_id);
};

/** a reference to a widget inside a widget.  nullptr means the current
    (root) widget is being referenced */
struct widget_ref {
    const struct widget_ref *next;

    const char *id;
};

static constexpr char WIDGET_REF_SEPARATOR = ':';
#define WIDGET_REF_SEPARATOR_S ":"

static inline const char *
widget_get_path_info(const struct widget *widget)
{
    return widget->from_request.path_info != nullptr
        ? widget->from_request.path_info
        : widget->path_info;
}

static inline bool
widget_has_default_view(const struct widget *widget)
{
    return widget->view != nullptr;
}

/**
 * Returns the view that will be used according to the widget class
 * and the view specification in the parent.  It ignores the view name
 * from the request.
 */
static inline const WidgetView *
widget_get_default_view(const struct widget *widget)
{
    return widget->view;
}

/**
 * Is the default view a container?
 */
gcc_pure
bool
widget_is_container_by_default(const struct widget *widget);

gcc_pure
static inline const WidgetView *
widget_get_view(const struct widget *widget)
{
    return widget->from_request.view;
}

/**
 * Does the effective view enable the HTML processor?
 */
gcc_pure
bool
widget_has_processor(const struct widget *widget);

/**
 * Is the effective view a container?
 */
gcc_pure
bool
widget_is_container(const struct widget *widget);

/**
 * Returns the view that is used to determine the address of the
 * server.
 */
static inline const WidgetView *
widget_get_address_view(const struct widget *widget)
{
    return widget_get_default_view(widget);
}

/**
 * Returns the view that is used to determine the transformations of
 * the response.
 */
static inline const WidgetView *
widget_get_transformation_view(const struct widget *widget)
{
    return widget_get_view(widget);
}

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
    if (widget->lazy.address == nullptr)
        widget->lazy.address = widget_determine_address(widget, true);

    return widget->lazy.address;
}

gcc_pure
static inline const struct resource_address *
widget_stateless_address(struct widget *widget)
{
    if (widget->lazy.stateless_address == nullptr)
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
