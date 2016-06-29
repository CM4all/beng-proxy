/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_HXX
#define BENG_PROXY_WIDGET_HXX

#include "util/StringView.hxx"

#include <inline/compiler.h>
#include <http/method.h>

#include <boost/intrusive/slist.hpp>

struct pool;
class Istream;
class StringMap;
struct StringView;
struct processor_env;
struct parsed_uri;
struct RealmSession;
struct WidgetSession;
struct WidgetView;
struct WidgetClass;
struct ResourceAddress;
struct WidgetResolver;

/**
 * A widget instance.
 */
struct Widget final
    : boost::intrusive::slist_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    boost::intrusive::slist<Widget,
                            boost::intrusive::constant_time_size<false>> children;

    Widget *parent;

    struct pool *pool;

    const char *class_name;

    /**
     * The widget class.  May be nullptr if the #class_name hasn't been
     * looked up yet.
     */
    const WidgetClass *cls;

    /**
     * The object that is currently requesting the widget class from
     * the translation server.
     */
    WidgetResolver *resolver;

    /** the widget's instance id, as specified in the template */
    const char *id;

    /** in which form should this widget be displayed? */
    enum {
        WIDGET_DISPLAY_INLINE,
        WIDGET_DISPLAY_NONE,
    } display;

    /**
     * Widget attributes specified by the template.  Some of them can
     * be overridden by the HTTP client.
     */
    struct FromTemplate {
        /** the path info as specified in the template */
        const char *path_info;

        /** the query string as specified in the template */
        const char *query_string;

        /** HTTP request headers specified in the template */
        StringMap *headers;

        /** the name of the view specified in the template */
        const char *view_name;

        /**
         * The view that was specified in the template.  This attribute is
         * undefined before the widget resolver finishes.  Being nullptr is a
         * fatal error, and means that no operation is possible on this
         * widget.
         */
        const WidgetView *view;
    } from_template;

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
        StringView query_string;

        /**
         * The request's HTTP method if the widget is focused.  Falls
         * back to HTTP_METHOD_GET if the widget is not focused.
         */
        http_method_t method;

        /** the request body (from processor_env.body) */
        Istream *body;

        /**
         * The view requested by the client.  If no view was
         * explicitly requested, then this is the view selected by the
         * template.  This attribute is undefined before the widget
         * resolver finishes.
         */
        const WidgetView *view;

        /**
         * Is this the "top frame" widget requested by the client?
         */
        bool frame;

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
        Istream *body;
    } for_focused;

    /**
     * Cached attributes that will be initialized lazily.
     */
    struct {
        const char *path;

        const char *prefix;

        const char *quoted_class_name;

        mutable const char *log_name;

        /** the address which is actually retrieved - this is the same
            as class->address, except when the user clicked on a
            relative link */
        const ResourceAddress *address;

        /**
         * The widget address including path_info and the query string
         * from the template.  See widget_stateless_address().
         */
        const ResourceAddress *stateless_address;
    } lazy;

    void Init(struct pool &_pool, const WidgetClass *_cls);
    void InitRoot(struct pool &_pool, const char *_id);

    void SetId(StringView _id);
    void SetClassName(StringView _class_name);

    const char *GetIdPath() const {
        return lazy.path;
    }

    const char *GetPrefix() const {
        return lazy.prefix;
    }

    const char *GetQuotedClassName() const {
        return lazy.quoted_class_name;
    }

    /**
     * Returns this widget's name for log/error messages.
     */
    gcc_pure
    const char *GetLogName() const;

    gcc_pure
    Widget *FindRoot() {
        Widget *w = this;
        while (w->parent != nullptr)
            w = w->parent;
        return w;
    }

    gcc_pure
    Widget *FindChild(const char *child_id);

    gcc_pure
    const char *GetDefaultPathInfo() const {
        return from_template.path_info;
    }

    gcc_pure
    const char *GetRequestedPathInfo() const {
        return from_request.path_info != nullptr
            ? from_request.path_info
            : from_template.path_info;
    }

    gcc_pure
    const char *GetPathInfo(bool stateful) const {
        return stateful ? GetRequestedPathInfo() : GetDefaultPathInfo();
    }

    gcc_pure
    bool HasDefaultView() const {
        return from_template.view != nullptr;
    }

    /**
     * Returns the view that will be used according to the widget
     * class and the view specification in the parent.  It ignores the
     * view name from the request.
     */
    const WidgetView *GetDefaultView() const {
        return from_template.view;
    }

    /**
     * Is the default view a container?
     */
    gcc_pure
    bool IsContainerByDefault() const;

    /**
     * Returns the view that is used to determine the address of the
     * server.
     */
    const WidgetView *GetAddressView() const {
        return GetDefaultView();
    }

    gcc_pure
    const WidgetView *GetEffectiveView() const {
        return from_request.view;
    }

    /**
     * Does the effective view enable the HTML processor?
     */
    gcc_pure
    bool HasProcessor() const;

    /**
     * Is the effective view a container?
     */
    gcc_pure
    bool IsContainer() const;

    /**
     * Returns the view that is used to determine the transformations of
     * the response.
     */
    const WidgetView *GetTransformationView() const {
        return GetEffectiveView();
    }

    /**
     * Determines whether it is allowed to embed the widget in a page with
     * the specified host name.
     */
    gcc_pure
    bool CheckHost(const char *host, const char *site_name) const;

    const ResourceAddress *DetermineAddress(bool stateful) const;

    gcc_pure
    const ResourceAddress *GetAddress() {
        if (lazy.address == nullptr)
            lazy.address = DetermineAddress(true);

        return lazy.address;
    }

    gcc_pure
    const ResourceAddress *GetStatelessAddress() {
        if (lazy.stateless_address == nullptr)
            lazy.stateless_address = DetermineAddress(false);

        return lazy.stateless_address;
    }

    gcc_pure
    ResourceAddress GetBaseAddress(struct pool &pool, bool stateful);

    gcc_pure
    const char *AbsoluteUri(struct pool &_pool, bool stateful,
                            StringView relative_uri);

    /**
     * Returns an URI relative to the widget base address.
     */
    gcc_pure
    StringView RelativeUri(struct pool &_pool, bool stateful,
                           StringView relative_uri);

    gcc_pure
    const char *ExternalUri(struct pool &_pool,
                            const struct parsed_uri *external_uri,
                            StringMap *args,
                            bool stateful,
                            StringView relative_uri,
                            const char *frame, const char *view);

    /**
     * Free important resources associated with the widget.  A widget
     * callback must call this function on a widget which it will not
     * send a HTTP request to.
     */
    void Cancel();
};

/** a reference to a widget inside a widget.  nullptr means the current
    (root) widget is being referenced */
struct widget_ref {
    const struct widget_ref *next;

    const char *id;
};

static constexpr char WIDGET_REF_SEPARATOR = ':';
#define WIDGET_REF_SEPARATOR_S ":"

/**
 * Returns the widget's session object.  The passed session object
 * must be locked.
 */
WidgetSession *
widget_get_session(Widget *widget, RealmSession *session,
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

/**
 * Recursion detection: check if the widget or its parent chain
 * contains the specified class name.
 */
gcc_pure
bool
widget_check_recursion(const Widget *widget);

#endif
