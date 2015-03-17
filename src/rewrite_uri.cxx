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
#include "strref_pool.hxx"
#include "uri-extract.h"
#include "tpool.h"
#include "escape_class.h"
#include "istream-escape.h"
#include "strmap.hxx"
#include "istream.h"
#include "istream_null.hxx"
#include "session.hxx"
#include "inline_widget.hxx"
#include "pool.hxx"

#include <daemon/log.h>

enum uri_mode
parse_uri_mode(const struct strref *s)
{
    if (strref_cmp_literal(s, "direct") == 0)
        return URI_MODE_DIRECT;
    else if (strref_cmp_literal(s, "focus") == 0)
        return URI_MODE_FOCUS;
    else if (strref_cmp_literal(s, "partial") == 0)
        return URI_MODE_PARTIAL;
    else if (strref_cmp_literal(s, "response") == 0)
        return URI_MODE_RESPONSE;
    else
        return URI_MODE_PARTIAL;
}

/*
 * The "real" rewriting code
 *
 */

static const char *
uri_replace_hostname(struct pool *pool, const char *uri, const char *hostname)
{
    assert(hostname != nullptr);

    if (*uri == '/')
        return p_strcat(pool,
                        "http://", hostname,
                        uri, nullptr);

    const char *start = strchr(uri, ':');
    if (start == nullptr || start[1] != '/' || start[1] != '/' || start[2] == '/')
        return uri;

    start += 2;

    const char *end;
    for (end = start;
         *end != 0 && *end != ':' && *end != '/';
         ++end) {
    }

    return p_strncat(pool,
                     uri, start - uri,
                     hostname, strlen(hostname),
                     end, strlen(end),
                     nullptr);
}

static const char *
uri_add_prefix(struct pool *pool, const char *uri, const char *absolute_uri,
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

        const char *host = uri_host_and_port(pool, absolute_uri);
        if (host == nullptr)
            return uri;

        return p_strcat(pool, "http://", untrusted_prefix, ".", host,
                        uri, nullptr);
    }

    const char *host = uri_host_and_port(pool, uri);
    if (host == nullptr)
        return uri;

    return uri_replace_hostname(pool, uri,
                                p_strcat(pool, untrusted_prefix, ".", host, nullptr));
}

static const char *
uri_add_site_suffix(struct pool *pool, const char *uri, const char *site_name,
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

    return p_strcat(pool, "http://", site_name, ".", untrusted_site_suffix,
                    path, nullptr);
}

/**
 * @return the new URI or nullptr if it is unchanged
 */
static const char *
do_rewrite_widget_uri(struct pool *pool, struct processor_env *env,
                      struct widget *widget,
                      const struct strref *value,
                      enum uri_mode mode, bool stateful,
                      const char *view)
{
    if (widget->cls->local_uri != nullptr && value != nullptr &&
        (value->length >= 2 && value->data[0] == '@' &&
         value->data[1] == '/'))
        /* relative to widget's "local URI" */
        return p_strncat(pool, widget->cls->local_uri,
                         strlen(widget->cls->local_uri),
                         value->data + 2, value->length - 2,
                         nullptr);

    const char *frame = nullptr;

    switch (mode) {
    case URI_MODE_DIRECT:
        assert(widget_get_address_view(widget) != nullptr);
        if (widget_get_address_view(widget)->address.type != RESOURCE_ADDRESS_HTTP)
            /* the browser can only contact HTTP widgets directly */
            return nullptr;

        return widget_absolute_uri(pool, widget, stateful, value);

    case URI_MODE_FOCUS:
        frame = strmap_get_checked(env->args, "frame");
        break;

    case URI_MODE_PARTIAL:
        frame = widget->GetIdPath();

        if (frame == nullptr)
            /* no widget_path available - "frame=" not possible*/
            return nullptr;
        break;

    case URI_MODE_RESPONSE:
        assert(false);
        gcc_unreachable();
    }

    const char *uri = widget_external_uri(pool, env->external_uri, env->args,
                                          widget, stateful,
                                          value,
                                          frame, view);
    if (uri == nullptr) {
        if (widget->id == nullptr)
            daemon_log(4, "Cannot rewrite URI for widget of type '%s': no id\n",
                       widget->class_name);
        else if (widget->GetIdPath() == nullptr)
            daemon_log(4, "Cannot rewrite URI for widget '%s', type '%s': broken id chain\n",
                       widget->id, widget->class_name);
        else
            daemon_log(4, "Base mismatch in widget '%s', type '%s'\n",
                       widget->GetIdPath(), widget->class_name);
        return nullptr;
    }

    if (widget->cls->untrusted_host != nullptr &&
        (env->untrusted_host == nullptr ||
         strcmp(widget->cls->untrusted_host, env->untrusted_host) != 0))
        uri = uri_replace_hostname(pool, uri, widget->cls->untrusted_host);
    else if (widget->cls->untrusted_prefix != nullptr)
        uri = uri_add_prefix(pool, uri, env->absolute_uri, env->untrusted_host,
                             widget->cls->untrusted_prefix);
    else if (widget->cls->untrusted_site_suffix != nullptr)
        uri = uri_add_site_suffix(pool, uri, env->site_name,
                                  env->untrusted_host,
                                  widget->cls->untrusted_site_suffix);

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

    /** buffer for #value */
    struct strref s;

    /** the value passed to rewrite_widget_uri() */
    struct strref *value;

    enum uri_mode mode;
    bool stateful;
    const char *view;

    const struct escape_class *escape;

    struct istream *delayed;
};

static void
class_lookup_callback(void *ctx)
{
    struct rewrite_widget_uri *rwu = (struct rewrite_widget_uri *)ctx;

    const struct strref *value = rwu->value;
    bool escape = false;
    if (rwu->widget->cls != nullptr &&
        widget_has_default_view(rwu->widget)) {
        const char *uri;

        if (rwu->widget->session_sync_pending) {
            struct session *session = session_get(rwu->env->session_id);
            if (session != nullptr) {
                widget_sync_session(rwu->widget, session);
                session_put(session);
            } else
                rwu->widget->session_sync_pending = false;
        }

        struct pool_mark_state mark;
        struct strref unescaped;
        if (value != nullptr && strref_chr(value, '&') != nullptr) {
            pool_mark(tpool, &mark);
            char *unescaped2 = strref_set_dup(tpool, &unescaped, value);
            unescaped.length = unescape_inplace(rwu->escape,
                                                unescaped2, unescaped.length);
            value = &unescaped;
        }

        uri = do_rewrite_widget_uri(rwu->pool, rwu->env,
                                    rwu->widget,
                                    value, rwu->mode, rwu->stateful,
                                    rwu->view);

        if (value == &unescaped)
            pool_rewind(tpool, &mark);

        if (uri != nullptr) {
            strref_set_c(&rwu->s, uri);
            value = &rwu->s;
            escape = true;
        }
    }

    struct istream *istream;
    if (value != nullptr) {
        istream = istream_memory_new(rwu->pool,
                                     value->data, value->length);

        if (escape && rwu->escape != nullptr)
            istream = istream_escape_new(rwu->pool, istream, rwu->escape);
    } else
        istream = istream_null_new(rwu->pool);

    istream_delayed_set(rwu->delayed,
                        istream);
    if (istream_has_handler(rwu->delayed))
        istream_read(rwu->delayed);
}


/*
 * Constructor: optionally load class, and then call
 * do_rewrite_widget_uri().
 *
 */

struct istream *
rewrite_widget_uri(struct pool *pool, struct pool *widget_pool,
                   struct processor_env *env,
                   struct tcache *translate_cache,
                   struct widget *widget,
                   const struct strref *value,
                   enum uri_mode mode, bool stateful,
                   const char *view,
                   const struct escape_class *escape)
{
    if (value != nullptr && uri_has_protocol(value->data, value->length))
        /* can't rewrite if the specified URI is absolute */
        return nullptr;

    if (mode == URI_MODE_RESPONSE) {
        struct istream *istream = embed_inline_widget(pool, env, true, widget);
        if (escape != nullptr)
            istream = istream_escape_new(pool, istream, escape);
        return istream;
    }

    const char *uri;

    if (widget->cls != nullptr) {
        if (!widget_has_default_view(widget))
            /* refuse to rewrite URIs when an invalid view name was
               specified */
            return nullptr;

        struct pool_mark_state mark;
        struct strref unescaped;
        if (escape != nullptr && value != nullptr &&
            unescape_find(escape, value->data, value->length) != nullptr) {
            pool_mark(tpool, &mark);
            char *unescaped2 = strref_set_dup(tpool, &unescaped, value);
            unescaped.length = unescape_inplace(escape,
                                                unescaped2, unescaped.length);
            value = &unescaped;
        }

        uri = do_rewrite_widget_uri(pool, env, widget, value, mode, stateful,
                                    view);
        if (value == &unescaped)
            pool_rewind(tpool, &mark);

        if (uri == nullptr)
            return nullptr;

        struct istream *istream = istream_string_new(pool, uri);
        if (escape != nullptr)
            istream = istream_escape_new(pool, istream, escape);

        return istream;
    } else {
        auto rwu = NewFromPool<struct rewrite_widget_uri>(*pool);

        rwu->pool = pool;
        rwu->env = env;
        rwu->widget = widget;

        if (value != nullptr) {
            strref_set_dup(pool, &rwu->s, value);
            rwu->value = &rwu->s;
        } else
            rwu->value = nullptr;

        rwu->mode = mode;
        rwu->stateful = stateful;
        rwu->view = view != NULL
            ? (*view != 0 ? p_strdup(pool, view) : "")
            : NULL;
        rwu->escape = escape;
        rwu->delayed = istream_delayed_new(pool);

        widget_resolver_new(pool, widget_pool,
                            widget,
                            translate_cache,
                            class_lookup_callback, rwu,
                            istream_delayed_async_ref(rwu->delayed));
        return rwu->delayed;
    }
}
