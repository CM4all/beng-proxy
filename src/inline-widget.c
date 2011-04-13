/*
 * Embed a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "inline-widget.h"
#include "widget-http.h"
#include "processor.h"
#include "widget.h"
#include "widget-class.h"
#include "widget-resolver.h"
#include "async.h"
#include "global.h"
#include "http-util.h"
#include "strref-pool.h"
#include "strref2.h"

#include <daemon/log.h>

#include <assert.h>

struct inline_widget {
    pool_t pool;
    struct processor_env *env;
    struct widget *widget;

    istream_t delayed;
};

static GQuark
widget_quark(void)
{
    return g_quark_from_static_string("widget");
}

static void
inline_widget_close(struct inline_widget *iw, GError *error)
{
    istream_delayed_set_abort(iw->delayed, error);
}

/**
 * Ensure that a widget has the correct type for embedding it into a
 * HTML/XML document.  Returns NULL (and closes body) if that is
 * impossible.
 */
static istream_t
widget_response_format(pool_t pool, const struct widget *widget,
                       struct strmap **headers_r, istream_t body,
                       GError **error_r)
{
    struct strmap *headers = *headers_r;
    const char *p, *content_type;
    struct strref *charset, charset_buffer;

    assert(body != NULL);

    p = strmap_get_checked(headers, "content-encoding");
    if (p != NULL && strcmp(p, "identity") != 0) {
        g_set_error(error_r, widget_quark(), 0,
                    "widget '%s' sent non-identity response, cannot embed",
                    widget_path(widget));
        istream_close_unused(body);
        return NULL;
    }

    content_type = strmap_get_checked(headers, "content-type");

    if (content_type == NULL ||
        (strncmp(content_type, "text/", 5) != 0 &&
         strncmp(content_type, "application/xhtml+xml", 21) != 0)) {
        g_set_error(error_r, widget_quark(), 0,
                    "widget '%s' sent non-text response",
                    widget_path(widget));
        istream_close_unused(body);
        return NULL;
    }

    charset = http_header_param(&charset_buffer, content_type, "charset");
    if (charset != NULL && strref_lower_cmp_literal(charset, "utf-8") != 0 &&
        strref_lower_cmp_literal(charset, "utf8") != 0) {
        /* beng-proxy expects all widgets to send their HTML code in
           utf-8; this widget however used a different charset.
           Automatically convert it with istream_iconv */
        const char *charset2 = strref_dup(pool, charset);
        istream_t ic = istream_iconv_new(pool, body, "utf-8", charset2);
        if (ic == NULL) {
            g_set_error(error_r, widget_quark(), 0,
                        "widget '%s' sent unknown charset '%s'",
                        widget_path(widget), charset2);
            istream_close_unused(body);
            return NULL;
        }

        daemon_log(6, "widget '%s': charset conversion '%s' -> utf-8\n",
                   widget_path(widget), charset2);
        body = ic;

        headers = strmap_dup(pool, headers);
        strmap_set(headers, "content-type", "text/html; charset=utf-8");
    }

    if (strncmp(content_type, "text/", 5) == 0 &&
        strncmp(content_type + 5, "html", 4) != 0 &&
        strncmp(content_type + 5, "xml", 3) != 0) {
        /* convert text to HTML */

        daemon_log(6, "widget '%s': converting text to HTML\n",
                   widget_path(widget));

        body = istream_html_escape_new(pool, body);
        body = istream_cat_new(pool,
                               istream_string_new(pool,
                                                  "<pre class=\"beng_text_widget\">"),
                               body,
                               istream_string_new(pool, "</pre>"),
                               NULL);
    }

    *headers_r = headers;
    return body;
}

/*
 * HTTP response handler
 *
 */

static void
inline_widget_response(http_status_t status,
                       struct strmap *headers,
                       istream_t body, void *ctx)
{
    struct inline_widget *iw = ctx;

    if (!http_status_is_success(status)) {
        /* the HTTP status code returned by the widget server is
           non-successful - don't embed this widget into the
           template */
        if (body != NULL)
            istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), 0,
                        "response status %d from widget '%s'",
                        status, widget_path(iw->widget));
        inline_widget_close(iw, error);
        return;
    }

    if (body != NULL) {
        /* check if the content-type is correct for embedding into
           a template, and convert if possible */
        GError *error = NULL;
        body = widget_response_format(iw->pool, iw->widget,
                                      &headers, body, &error);
        if (body == NULL) {
            inline_widget_close(iw, error);
            return;
        }
    } else
        body = istream_null_new(iw->pool);

    istream_delayed_set(iw->delayed, body);

    if (istream_has_handler(iw->delayed))
        istream_read(iw->delayed);
}

static void
inline_widget_abort(GError *error, void *ctx)
{
    struct inline_widget *iw = ctx;

    inline_widget_close(iw, error);
}

const struct http_response_handler inline_widget_response_handler = {
    .response = inline_widget_response,
    .abort = inline_widget_abort,
};


/*
 * internal
 *
 */

static void
inline_widget_set(struct inline_widget *iw)
{
    struct widget *widget = iw->widget;

    if (!widget_check_host(widget, iw->env->untrusted_host,
                           iw->env->site_name)) {
        GError *error =
            g_error_new(widget_quark(), 0,
                        "untrusted host name mismatch");
        widget_cancel(widget);
        istream_delayed_set_abort(iw->delayed, error);
        return;
    }

    if (widget->class->stateful) {
        struct session *session = session_get(iw->env->session_id);
        if (session != NULL) {
            widget_sync_session(widget, session);
            session_put(session);
        }
    }

    widget_http_request(iw->pool, iw->widget, iw->env,
                        &inline_widget_response_handler, iw,
                        istream_delayed_async_ref(iw->delayed));
}


/*
 * Widget resolver callback
 *
 */

static void
class_lookup_callback(void *_ctx)
{
    struct inline_widget *iw = _ctx;

    if (iw->widget->class != NULL) {
        inline_widget_set(iw);
    } else {
        GError *error =
            g_error_new(widget_quark(), 0,
                        "failed to look up widget class '%s'",
                        iw->widget->class_name);
        inline_widget_close(iw, error);
    }
}


/*
 * Constructor
 *
 */

istream_t
embed_inline_widget(pool_t pool, struct processor_env *env,
                    struct widget *widget)
{
    struct inline_widget *iw = p_malloc(pool, sizeof(*iw));
    istream_t hold;

    assert(pool != NULL);
    assert(env != NULL);
    assert(widget != NULL);

    if (widget->display == WIDGET_DISPLAY_NONE) {
        widget_cancel(widget);
        return NULL;
    }

    iw->pool = pool;
    iw->env = env;
    iw->widget = widget;
    iw->delayed = istream_delayed_new(pool);
    hold = istream_hold_new(pool, iw->delayed);

    if (widget->class == NULL)
        widget_resolver_new(pool, env->pool,
                            widget,
                            global_translate_cache,
                            class_lookup_callback, iw,
                            istream_delayed_async_ref(iw->delayed));
    else
        inline_widget_set(iw);

    return hold;
}
