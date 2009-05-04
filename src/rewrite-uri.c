/*
 * Rewrite URIs in templates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "rewrite-uri.h"
#include "widget.h"
#include "widget-resolver.h"
#include "strref-pool.h"
#include "uri-parser.h"

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
generate_widget_hostname(pool_t pool,
                         struct widget *widget,
                         const char *domain)
{
    assert(widget != NULL);
    assert(domain != NULL);

    return p_strcat(pool, widget_prefix(widget), ".", domain, NULL);
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
do_rewrite_widget_uri(pool_t pool,
                      const char *partition_domain,
                      const struct parsed_uri *external_uri,
                      struct strmap *args, struct widget *widget,
                      const struct strref *value,
                      enum uri_mode mode)
{
    const char *frame = NULL;
    bool raw = false;
    const char *uri;

    switch (mode) {
    case URI_MODE_DIRECT:
        if (widget->class->address.type != RESOURCE_ADDRESS_HTTP)
            /* the browser can only contact HTTP widgets directly */
            return NULL;

        return widget_absolute_uri(pool, widget, value);

    case URI_MODE_FOCUS:
        frame = current_frame(widget);
        break;

    case URI_MODE_PROXY:
        raw = true;
    case URI_MODE_PARTIAL:
    case URI_MODE_PARTITION:
        frame = widget_path(widget);

        if (frame == NULL)
            /* no widget_path available - "frame=" not possible*/
            return NULL;
        break;
    }

    uri = widget_external_uri(pool, external_uri, args,
                              widget,
                              mode == URI_MODE_FOCUS || value != NULL,
                              value,
                              frame, raw);
    if (mode == URI_MODE_PARTITION && partition_domain != NULL)
        uri = uri_replace_hostname(pool, uri,
                                   generate_widget_hostname(pool, widget,
                                                            partition_domain));
    else if (widget->class->host != NULL)
        uri = uri_replace_hostname(pool, uri, widget->class->host);

    return uri;
}


/*
 * widget_resolver callback
 *
 */

struct rewrite_widget_uri {
    pool_t pool;
    const char *partition_domain;
    const struct parsed_uri *external_uri;
    struct strmap *args;
    struct widget *widget;
    session_id_t session_id;

    /** buffer for #value */
    struct strref s;

    /** the value passed to rewrite_widget_uri() */
    struct strref *value;

    enum uri_mode mode;

    istream_t delayed;
};

static void
class_lookup_callback(void *ctx)
{
    struct rewrite_widget_uri *rwu = ctx;
    istream_t istream;

    if (rwu->widget->class != NULL) {
        struct session *session;
        const char *uri;

        if (rwu->widget->class->stateful) {
            session = session_get(rwu->session_id);
            if (session != NULL)
                widget_sync_session(rwu->widget, session);
        }

        uri = do_rewrite_widget_uri(rwu->pool,
                                    rwu->partition_domain, rwu->external_uri,
                                    rwu->args, rwu->widget,
                                    rwu->value, rwu->mode);
        if (uri != NULL) {
            strref_set_c(&rwu->s, uri);
            rwu->value = &rwu->s;
        }
    }

    if (rwu->value != NULL)
        istream = istream_memory_new(rwu->pool,
                                     rwu->value->data, rwu->value->length);
    else
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
                   const char *partition_domain,
                   const struct parsed_uri *external_uri,
                   struct strmap *args, struct widget *widget,
                   session_id_t session_id,
                   const struct strref *value,
                   enum uri_mode mode)
{
    const char *uri;

    if (widget->class != NULL) {
        uri = do_rewrite_widget_uri(pool, partition_domain, external_uri,
                                    args, widget, value, mode);
        if (uri == NULL)
            return NULL;

        return istream_string_new(pool, uri);
    } else {
        struct rewrite_widget_uri *rwu = p_malloc(pool, sizeof(*rwu));

        rwu->pool = pool;
        rwu->partition_domain = partition_domain;
        rwu->external_uri = external_uri;
        rwu->args = args;
        rwu->widget = widget;
        rwu->session_id = session_id;

        if (value != NULL) {
            strref_set_dup(pool, &rwu->s, value);
            rwu->value = &rwu->s;
        } else
            rwu->value = NULL;

        rwu->mode = mode;
        rwu->delayed = istream_delayed_new(pool);

        widget_resolver_new(pool, widget_pool,
                            widget,
                            translate_cache,
                            class_lookup_callback, rwu,
                            istream_delayed_async_ref(rwu->delayed));
        return rwu->delayed;
    }
}
