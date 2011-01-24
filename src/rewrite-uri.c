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
#include "html-escape.h"

/*
 * The "real" rewriting code
 *
 */

static const char *
current_frame(const struct widget *widget)
{
    do {
        if (widget->from_request.proxy)
            return widget_path(widget);

        widget = widget->parent;
    } while (widget != NULL);

    return NULL;
}

static const char *
uri_replace_hostname(pool_t pool, const char *uri, const char *hostname)
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
uri_add_prefix(pool_t pool, const char *uri, const char *absolute_uri,
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
do_rewrite_widget_uri(pool_t pool,
                      const char *absolute_uri,
                      const struct parsed_uri *external_uri,
                      const char *untrusted_host,
                      struct strmap *args, struct widget *widget,
                      const struct strref *value,
                      enum uri_mode mode, bool stateful)
{
    const char *frame = NULL;
    bool raw = false;
    const char *uri;

    switch (mode) {
    case URI_MODE_DIRECT:
        if (widget->class->address.type != RESOURCE_ADDRESS_HTTP)
            /* the browser can only contact HTTP widgets directly */
            return NULL;

        return widget_absolute_uri(pool, widget, stateful, value);

    case URI_MODE_FOCUS:
        frame = current_frame(widget);
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
                              frame, raw);
    if (uri == NULL)
        return NULL;

    if (widget->class->untrusted_host != NULL &&
        (untrusted_host == NULL ||
         strcmp(widget->class->untrusted_host, untrusted_host) != 0))
        uri = uri_replace_hostname(pool, uri, widget->class->untrusted_host);
    else if (widget->class->untrusted_prefix != NULL)
        uri = uri_add_prefix(pool, uri, absolute_uri, untrusted_host,
                             widget->class->untrusted_prefix);

    return uri;
}


/*
 * widget_resolver callback
 *
 */

struct rewrite_widget_uri {
    pool_t pool;
    const char *absolute_uri;
    const struct parsed_uri *external_uri;
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

    istream_t delayed;
};

static void
class_lookup_callback(void *ctx)
{
    struct rewrite_widget_uri *rwu = ctx;
    istream_t istream;

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
            unescaped.length = html_unescape_inplace(unescaped2, unescaped.length);
            rwu->value = &unescaped;
        }

        uri = do_rewrite_widget_uri(rwu->pool,
                                    rwu->absolute_uri,
                                    rwu->external_uri, rwu->untrusted_host,
                                    rwu->args, rwu->widget,
                                    rwu->value, rwu->mode, rwu->stateful);

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

        if (escape)
            istream = istream_html_escape_new(rwu->pool, istream);
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

istream_t
rewrite_widget_uri(pool_t pool, pool_t widget_pool,
                   struct tcache *translate_cache,
                   const char *absolute_uri,
                   const struct parsed_uri *external_uri,
                   const char *untrusted_host,
                   struct strmap *args, struct widget *widget,
                   session_id_t session_id,
                   const struct strref *value,
                   enum uri_mode mode, bool stateful)
{
    const char *uri;

    if (widget->class != NULL) {
        struct pool_mark mark;
        struct strref unescaped;
        if (value != NULL && strref_chr(value, '&') != NULL) {
            pool_mark(tpool, &mark);
            char *unescaped2 = strref_set_dup(tpool, &unescaped, value);
            unescaped.length = html_unescape_inplace(unescaped2, unescaped.length);
            value = &unescaped;
        }

        uri = do_rewrite_widget_uri(pool, absolute_uri, external_uri, untrusted_host,
                                    args, widget, value, mode, stateful);
        if (value == &unescaped)
            pool_rewind(tpool, &mark);

        if (uri == NULL)
            return NULL;

        istream_t istream = istream_string_new(pool, uri);
        istream = istream_html_escape_new(pool, istream);

        return istream;
    } else {
        struct rewrite_widget_uri *rwu = p_malloc(pool, sizeof(*rwu));

        rwu->pool = pool;
        rwu->external_uri = external_uri;
        rwu->absolute_uri = absolute_uri;
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
        rwu->delayed = istream_delayed_new(pool);

        widget_resolver_new(pool, widget_pool,
                            widget,
                            translate_cache,
                            class_lookup_callback, rwu,
                            istream_delayed_async_ref(rwu->delayed));
        return rwu->delayed;
    }
}
