/*
 * Rewrite URIs in templates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "rewrite_uri.hxx"
#include "penv.hxx"
#include "widget.hxx"
#include "widget_request.hxx"
#include "widget_resolver.hxx"
#include "widget_class.hxx"
#include "uri/uri_extract.hxx"
#include "tpool.hxx"
#include "escape_class.hxx"
#include "istream_escape.hxx"
#include "istream/TimeoutIstream.hxx"
#include "istream/istream_delayed.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "strmap.hxx"
#include "session.hxx"
#include "inline_widget.hxx"
#include "pool.hxx"
#include "pbuffer.hxx"
#include "util/StringView.hxx"

#include <daemon/log.h>

enum uri_mode
parse_uri_mode(const StringView s)
{
    if (s.EqualsLiteral("direct"))
        return URI_MODE_DIRECT;
    else if (s.EqualsLiteral("focus"))
        return URI_MODE_FOCUS;
    else if (s.EqualsLiteral("partial"))
        return URI_MODE_PARTIAL;
    else if (s.EqualsLiteral("response"))
        return URI_MODE_RESPONSE;
    else
        return URI_MODE_PARTIAL;
}

/*
 * The "real" rewriting code
 *
 */

static const char *
uri_replace_hostname(struct pool &pool, const char *uri, const char *hostname)
{
    assert(hostname != nullptr);

    const auto old_host = uri_host_and_port(uri);
    if (old_host.IsNull())
        return *uri == '/'
            ? p_strcat(&pool,
                       "//", hostname,
                       uri, nullptr)
            : nullptr;

    const char *colon = (const char *)memchr(old_host.data, ':', old_host.size);
    const char *end = colon != nullptr ? colon : old_host.end();

    return p_strncat(&pool,
                     uri, old_host.data - uri,
                     hostname, strlen(hostname),
                     end, strlen(end),
                     nullptr);
}

static const char *
uri_add_prefix(struct pool &pool, const char *uri, const char *absolute_uri,
               const char *untrusted_host, const char *untrusted_prefix)
{
    assert(untrusted_prefix != nullptr);

    if (untrusted_host != nullptr)
        /* this request comes from an untrusted host - either we're
           already in the correct prefix (no-op), or this is a
           different untrusted domain (not supported) */
        return uri;

    if (*uri == '/') {
        if (absolute_uri == nullptr)
            /* unknown old host name, we cannot do anything useful */
            return uri;

        const auto host = uri_host_and_port(absolute_uri);
        if (host.IsNull())
            return uri;

        return p_strncat(&pool, absolute_uri, size_t(host.data - absolute_uri),
                         untrusted_prefix, strlen(untrusted_prefix),
                         ".", size_t(1),
                         host.data, host.size,
                         uri, strlen(uri),
                         nullptr);
    }

    const auto host = uri_host_and_port(uri);
    if (host.IsNull())
        return uri;

    return p_strncat(&pool, uri, size_t(host.data - uri),
                     untrusted_prefix, strlen(untrusted_prefix),
                     ".", size_t(1),
                     host.data, strlen(host.data),
                     nullptr);
}

static const char *
uri_add_site_suffix(struct pool &pool, const char *uri, const char *site_name,
                    const char *untrusted_host,
                    const char *untrusted_site_suffix)
{
    assert(untrusted_site_suffix != nullptr);

    if (untrusted_host != nullptr)
        /* this request comes from an untrusted host - either we're
           already in the correct suffix (no-op), or this is a
           different untrusted domain (not supported) */
        return uri;

    if (site_name == nullptr)
        /* we don't know the site name of this request; we cannot do
           anything, so we're just returning the unmodified URI, which
           will render an error message */
        return uri;

    const char *path = uri_path(uri);
    if (path == nullptr)
        /* without an absolute path, we cannot build a new absolute
           URI */
        return uri;

    return p_strcat(&pool, "//", site_name, ".", untrusted_site_suffix,
                    path, nullptr);
}

static const char *
uri_add_raw_site_suffix(struct pool &pool, const char *uri, const char *site_name,
                        const char *untrusted_host,
                        const char *untrusted_raw_site_suffix)
{
    assert(untrusted_raw_site_suffix != nullptr);

    if (untrusted_host != nullptr)
        /* this request comes from an untrusted host - either we're
           already in the correct suffix (no-op), or this is a
           different untrusted domain (not supported) */
        return uri;

    if (site_name == nullptr)
        /* we don't know the site name of this request; we cannot do
           anything, so we're just returning the unmodified URI, which
           will render an error message */
        return uri;

    const char *path = uri_path(uri);
    if (path == nullptr)
        /* without an absolute path, we cannot build a new absolute
           URI */
        return uri;

    return p_strcat(&pool, "//", site_name, untrusted_raw_site_suffix,
                    path, nullptr);
}

/**
 * @return the new URI or nullptr if it is unchanged
 */
static const char *
do_rewrite_widget_uri(struct pool &pool, struct processor_env &env,
                      struct widget &widget,
                      StringView value,
                      enum uri_mode mode, bool stateful,
                      const char *view)
{
    if (widget.cls->local_uri != nullptr &&
        value.size >= 2 && value[0] == '@' && value[1] == '/')
        /* relative to widget's "local URI" */
        return p_strncat(&pool, widget.cls->local_uri,
                         strlen(widget.cls->local_uri),
                         value.data + 2, value.size - 2,
                         nullptr);

    const char *frame = nullptr;

    switch (mode) {
    case URI_MODE_DIRECT:
        assert(widget_get_address_view(&widget) != nullptr);
        if (widget_get_address_view(&widget)->address.type != ResourceAddress::Type::HTTP)
            /* the browser can only contact HTTP widgets directly */
            return nullptr;

        return widget_absolute_uri(&pool, &widget, stateful, value);

    case URI_MODE_FOCUS:
        frame = strmap_get_checked(env.args, "frame");
        break;

    case URI_MODE_PARTIAL:
        frame = widget.GetIdPath();

        if (frame == nullptr)
            /* no widget_path available - "frame=" not possible*/
            return nullptr;
        break;

    case URI_MODE_RESPONSE:
        assert(false);
        gcc_unreachable();
    }

    const char *uri = widget_external_uri(&pool, env.external_uri, env.args,
                                          &widget, stateful,
                                          value,
                                          frame, view);
    if (uri == nullptr) {
        if (widget.id == nullptr)
            daemon_log(4, "Cannot rewrite URI for widget '%s': no id\n",
                       widget.GetLogName());
        else if (widget.GetIdPath() == nullptr)
            daemon_log(4, "Cannot rewrite URI for widget '%s': broken id chain\n",
                       widget.GetLogName());
        else
            daemon_log(4, "Base mismatch in widget '%s': %.*s\n",
                       widget.GetLogName(),
                       int(value.size), value.data);
        return nullptr;
    }

    if (widget.cls->untrusted_host != nullptr &&
        (env.untrusted_host == nullptr ||
         strcmp(widget.cls->untrusted_host, env.untrusted_host) != 0))
        uri = uri_replace_hostname(pool, uri, widget.cls->untrusted_host);
    else if (widget.cls->untrusted_prefix != nullptr)
        uri = uri_add_prefix(pool, uri, env.absolute_uri, env.untrusted_host,
                             widget.cls->untrusted_prefix);
    else if (widget.cls->untrusted_site_suffix != nullptr)
        uri = uri_add_site_suffix(pool, uri, env.site_name,
                                  env.untrusted_host,
                                  widget.cls->untrusted_site_suffix);
    else if (widget.cls->untrusted_raw_site_suffix != nullptr)
        uri = uri_add_raw_site_suffix(pool, uri, env.site_name,
                                      env.untrusted_host,
                                      widget.cls->untrusted_raw_site_suffix);

    return uri;
}


/*
 * widget_resolver callback
 *
 */

struct rewrite_widget_uri {
    struct pool *pool;
    struct processor_env *env;
    struct widget *widget;

    /** the value passed to rewrite_widget_uri() */
    StringView value;

    enum uri_mode mode;
    bool stateful;
    const char *view;

    const struct escape_class *escape;

    Istream *delayed, *timeout;
};

static void
class_lookup_callback(void *ctx)
{
    struct rewrite_widget_uri *rwu = (struct rewrite_widget_uri *)ctx;

    StringView value = rwu->value;
    bool escape = false;
    if (rwu->widget->cls != nullptr &&
        widget_has_default_view(rwu->widget)) {
        const char *uri;

        if (rwu->widget->session_sync_pending) {
            auto *session = session_get(rwu->env->session_id);
            if (session != nullptr) {
                widget_sync_session(rwu->widget, session);
                session_put(session);
            } else
                rwu->widget->session_sync_pending = false;
        }

        struct pool_mark_state mark;
        bool is_unescaped = value.Find('&') != nullptr;
        if (is_unescaped) {
            pool_mark(tpool, &mark);
            char *unescaped = (char *)p_memdup(tpool, value.data, value.size);
            value.size = unescape_inplace(rwu->escape, unescaped, value.size);
            value.data = unescaped;
        }

        uri = do_rewrite_widget_uri(*rwu->pool, *rwu->env,
                                    *rwu->widget,
                                    value, rwu->mode, rwu->stateful,
                                    rwu->view);

        if (is_unescaped)
            pool_rewind(tpool, &mark);

        if (uri != nullptr) {
            value = uri;
            escape = true;
        }
    }

    Istream *istream;
    if (!value.IsEmpty()) {
        istream = istream_memory_new(rwu->pool, value.data, value.size);

        if (escape && rwu->escape != nullptr)
            istream = istream_escape_new(*rwu->pool, *istream, *rwu->escape);
    } else
        istream = istream_null_new(rwu->pool);

    istream_delayed_set(*rwu->delayed,
                        *istream);
    if (rwu->timeout->HasHandler())
        rwu->timeout->Read();
}


/*
 * Constructor: optionally load class, and then call
 * do_rewrite_widget_uri().
 *
 */

Istream *
rewrite_widget_uri(struct pool &pool,
                   struct processor_env &env,
                   struct tcache &translate_cache,
                   struct widget &widget,
                   StringView value,
                   enum uri_mode mode, bool stateful,
                   const char *view,
                   const struct escape_class *escape)
{
    if (uri_has_authority(value))
        /* can't rewrite if the specified URI is absolute */
        return nullptr;

    if (mode == URI_MODE_RESPONSE) {
        Istream *istream = embed_inline_widget(pool, env, true,
                                                      widget);
        if (escape != nullptr)
            istream = istream_escape_new(pool, *istream, *escape);
        return istream;
    }

    const char *uri;

    if (widget.cls != nullptr) {
        if (!widget_has_default_view(&widget))
            /* refuse to rewrite URIs when an invalid view name was
               specified */
            return nullptr;

        struct pool_mark_state mark;
        bool is_unescaped = false;
        if (escape != nullptr && !value.IsNull() &&
            unescape_find(escape, value) != nullptr) {
            pool_mark(tpool, &mark);
            char *unescaped = (char *)p_memdup(tpool, value.data, value.size);
            value.size = unescape_inplace(escape, unescaped, value.size);
            value.data = unescaped;
            is_unescaped = true;
        }

        uri = do_rewrite_widget_uri(pool, env, widget, value, mode, stateful,
                                    view);
        if (is_unescaped)
            pool_rewind(tpool, &mark);

        if (uri == nullptr)
            return nullptr;

        Istream *istream = istream_string_new(&pool, uri);
        if (escape != nullptr)
            istream = istream_escape_new(pool, *istream, *escape);

        return istream;
    } else {
        auto rwu = NewFromPool<struct rewrite_widget_uri>(pool);

        rwu->pool = &pool;
        rwu->env = &env;
        rwu->widget = &widget;
        rwu->value = DupBuffer(pool, value);
        rwu->mode = mode;
        rwu->stateful = stateful;
        rwu->view = view != NULL
            ? (*view != 0 ? p_strdup(&pool, view) : "")
            : NULL;
        rwu->escape = escape;
        rwu->delayed = istream_delayed_new(&pool);
        rwu->timeout = NewTimeoutIstream(pool, *rwu->delayed,
                                         *env.event_loop,
                                         inline_widget_timeout);

        widget_resolver_new(pool,
                            widget,
                            translate_cache,
                            class_lookup_callback, rwu,
                            *istream_delayed_async_ref(*rwu->delayed));
        return rwu->timeout;
    }
}
