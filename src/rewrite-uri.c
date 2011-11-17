/*
 * Rewrite URIs in templates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "rewrite-uri.h"
#include "widget.h"
#include "widget-resolver.h"
#include "widget-class.h"
#include "strref-pool.h"
#include "uri-parser.h"
#include "uri-extract.h"
#include "tpool.h"
#include "escape_class.h"
#include "istream-escape.h"
#include "strmap.h"
#include "istream.h"

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
    else if (strref_cmp_literal(s, "partition") == 0)
        /* deprecated */
        return URI_MODE_PARTIAL;
    else if (strref_cmp_literal(s, "proxy") == 0)
        return URI_MODE_PROXY;
    else
        return URI_MODE_DIRECT;
}

/*
 * The "real" rewriting code
 *
 */

static const char *
uri_replace_hostname(struct pool *pool, const char *uri, const char *hostname)
{
    const char *start, *end;

    assert(hostname != NULL);

    if (*uri == '/')
        return p_strcat(pool,
                        "http://", hostname,
                        uri, NULL);

    start = strchr(uri, ':');
    if (start == NULL || start[1] != '/' || start[1] != '/' || start[2] == '/')
        return uri;

    start += 2;

    for (end = start;
         *end != 0 && *end != ':' && *end != '/';
         ++end) {
    }

    return p_strncat(pool,
                     uri, start - uri,
                     hostname, strlen(hostname),
                     end, strlen(end),
                     NULL);
}

static const char *
uri_add_prefix(struct pool *pool, const char *uri, const char *absolute_uri,
               const char *untrusted_host, const char *untrusted_prefix)
{
    assert(untrusted_prefix != NULL);

    if (untrusted_host != NULL)
        /* this request comes from an untrusted host - either we're
           already in the correct prefix (no-op), or this is a
           different untrusted domain (not supported) */
        return uri;

    if (*uri == '/') {
        if (absolute_uri == NULL)
            /* unknown old host name, we cannot do anything useful */
            return uri;

        const char *host = uri_host_and_port(pool, absolute_uri);
        if (host == NULL)
            return uri;

        return p_strcat(pool, "http://", untrusted_prefix, ".", host,
                        uri, NULL);
    }

    const char *host = uri_host_and_port(pool, uri);
    if (host == NULL)
        return uri;

    return uri_replace_hostname(pool, uri,
                                p_strcat(pool, untrusted_prefix, ".", host, NULL));
}

static const char *
uri_add_site_suffix(struct pool *pool, const char *uri, const char *site_name,
                    const char *untrusted_host,
                    const char *untrusted_site_suffix)
{
    assert(untrusted_site_suffix != NULL);

    if (untrusted_host != NULL)
        /* this request comes from an untrusted host - either we're
           already in the correct suffix (no-op), or this is a
           different untrusted domain (not supported) */
        return uri;

    if (site_name == NULL)
        /* we don't know the site name of this request; we cannot do
           anything, so we're just returning the unmodified URI, which
           will render an error message */
        return uri;

    const char *path = uri_path(uri);
    if (path == NULL)
        /* without an absolute path, we cannot build a new absolute
           URI */
        return uri;

    return p_strcat(pool, "http://", site_name, ".", untrusted_site_suffix,
                    path, NULL);
}

static const char *
do_rewrite_widget_uri(struct pool *pool,
                      const char *absolute_uri,
                      const struct parsed_uri *external_uri,
                      const char *site_name,
                      const char *untrusted_host,
                      struct strmap *args, struct widget *widget,
                      const struct strref *value,
                      enum uri_mode mode, bool stateful,
                      const char *view)
{
    const char *frame = NULL;
    bool raw = false;
    const char *uri;

    switch (mode) {
    case URI_MODE_DIRECT:
        if (widget_get_view(widget) == NULL ||
            widget_get_view(widget)->address.type != RESOURCE_ADDRESS_HTTP)
            /* the browser can only contact HTTP widgets directly */
            return NULL;

        return widget_absolute_uri(pool, widget, stateful, value);

    case URI_MODE_FOCUS:
        frame = strmap_get_checked(args, "frame");
        break;

    case URI_MODE_PROXY:
        raw = true;
    case URI_MODE_PARTIAL:
        frame = widget_path(widget);

        if (frame == NULL)
            /* no widget_path available - "frame=" not possible*/
            return NULL;
        break;
    }

    uri = widget_external_uri(pool, external_uri, args,
                              widget, stateful,
                              value,
                              frame, view, raw);
    if (uri == NULL) {
        if (widget->id == NULL)
            daemon_log(4, "Cannot rewrite URI for widget of type '%s': no id\n",
                       widget->class_name);
        else if (widget_path(widget) == NULL)
            daemon_log(4, "Cannot rewrite URI for widget '%s', type '%s': broken id chain\n",
                       widget->id, widget->class_name);
        else
            daemon_log(4, "Base mismatch in widget '%s', type '%s'\n",
                       widget_path(widget), widget->class_name);
        return NULL;
    }

    if (widget->class->untrusted_host != NULL &&
        (untrusted_host == NULL ||
         strcmp(widget->class->untrusted_host, untrusted_host) != 0))
        uri = uri_replace_hostname(pool, uri, widget->class->untrusted_host);
    else if (widget->class->untrusted_prefix != NULL)
        uri = uri_add_prefix(pool, uri, absolute_uri, untrusted_host,
                             widget->class->untrusted_prefix);
    else if (widget->class->untrusted_site_suffix != NULL)
        uri = uri_add_site_suffix(pool, uri, site_name, untrusted_host,
                                  widget->class->untrusted_site_suffix);

    return uri;
}


/*
 * widget_resolver callback
 *
 */

struct rewrite_widget_uri {
    struct pool *pool;
    const char *absolute_uri;
    const struct parsed_uri *external_uri;
    const char *site_name;
    const char *untrusted_host;
    struct strmap *args;
    struct widget *widget;
    session_id_t session_id;

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
    struct rewrite_widget_uri *rwu = ctx;
    struct istream *istream;

    bool escape = false;
    if (rwu->widget->class != NULL) {
        const char *uri;

        if (rwu->widget->class->stateful) {
            struct session *session = session_get(rwu->session_id);
            if (session != NULL) {
                widget_sync_session(rwu->widget, session);
                session_put(session);
            }
        }

        struct pool_mark mark;
        struct strref unescaped;
        if (rwu->value != NULL && strref_chr(rwu->value, '&') != NULL) {
            pool_mark(tpool, &mark);
            char *unescaped2 = strref_set_dup(tpool, &unescaped, rwu->value);
            unescaped.length = unescape_inplace(rwu->escape,
                                                unescaped2, unescaped.length);
            rwu->value = &unescaped;
        }

        uri = do_rewrite_widget_uri(rwu->pool,
                                    rwu->absolute_uri, rwu->external_uri,
                                    rwu->site_name, rwu->untrusted_host,
                                    rwu->args, rwu->widget,
                                    rwu->value, rwu->mode, rwu->stateful,
                                    rwu->view);

        if (rwu->value == &unescaped)
            pool_rewind(tpool, &mark);

        if (uri != NULL) {
            strref_set_c(&rwu->s, uri);
            rwu->value = &rwu->s;
            escape = true;
        }
    }

    if (rwu->value != NULL) {
        istream = istream_memory_new(rwu->pool,
                                     rwu->value->data, rwu->value->length);

        if (escape && rwu->escape != NULL)
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
                   struct tcache *translate_cache,
                   const char *absolute_uri,
                   const struct parsed_uri *external_uri,
                   const char *site_name,
                   const char *untrusted_host,
                   struct strmap *args, struct widget *widget,
                   session_id_t session_id,
                   const struct strref *value,
                   enum uri_mode mode, bool stateful,
                   const char *view,
                   const struct escape_class *escape)
{
    const char *uri;

    if (widget->class != NULL) {
        struct pool_mark mark;
        struct strref unescaped;
        if (escape != NULL && value != NULL &&
            unescape_find(escape, value->data, value->length) != NULL) {
            pool_mark(tpool, &mark);
            char *unescaped2 = strref_set_dup(tpool, &unescaped, value);
            unescaped.length = unescape_inplace(escape,
                                                unescaped2, unescaped.length);
            value = &unescaped;
        }

        uri = do_rewrite_widget_uri(pool, absolute_uri, external_uri,
                                    site_name, untrusted_host,
                                    args, widget, value, mode, stateful, view);
        if (value == &unescaped)
            pool_rewind(tpool, &mark);

        if (uri == NULL)
            return NULL;

        struct istream *istream = istream_string_new(pool, uri);
        if (escape != NULL)
            istream = istream_escape_new(pool, istream, escape);

        return istream;
    } else {
        struct rewrite_widget_uri *rwu = p_malloc(pool, sizeof(*rwu));

        rwu->pool = pool;
        rwu->external_uri = external_uri;
        rwu->absolute_uri = absolute_uri;
        rwu->site_name = site_name;
        rwu->untrusted_host = untrusted_host;
        rwu->args = args;
        rwu->widget = widget;
        rwu->session_id = session_id;

        if (value != NULL) {
            strref_set_dup(pool, &rwu->s, value);
            rwu->value = &rwu->s;
        } else
            rwu->value = NULL;

        rwu->mode = mode;
        rwu->stateful = stateful;
        rwu->view = view;
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
