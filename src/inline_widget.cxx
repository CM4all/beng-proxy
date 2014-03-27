/*
 * Embed a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "inline_widget.hxx"
#include "widget_http.hxx"
#include "widget-quark.h"
#include "penv.h"
#include "widget.h"
#include "widget-class.h"
#include "widget-resolver.h"
#include "widget-approval.h"
#include "widget-request.h"
#include "async.h"
#include "global.h"
#include "http_util.h"
#include "http_response.h"
#include "strref-pool.h"
#include "strref2.h"
#include "strmap.h"
#include "http_response.h"
#include "istream.h"
#include "istream_pause.h"

#include <daemon/log.h>

#include <assert.h>

struct inline_widget {
    struct pool *pool;
    struct processor_env *env;
    struct widget *widget;

    struct istream *delayed;
};

static void
inline_widget_close(struct inline_widget *iw, GError *error)
{
    istream_delayed_set_abort(iw->delayed, error);
}

/**
 * Ensure that a widget has the correct type for embedding it into a
 * HTML/XML document.  Returns nullptr (and closes body) if that is
 * impossible.
 */
static struct istream *
widget_response_format(struct pool *pool, const struct widget *widget,
                       struct strmap **headers_r, struct istream *body,
                       GError **error_r)
{
    struct strmap *headers = *headers_r;
    const char *p, *content_type;
    struct strref *charset, charset_buffer;

    assert(body != nullptr);

    p = strmap_get_checked(headers, "content-encoding");
    if (p != nullptr && strcmp(p, "identity") != 0) {
        g_set_error(error_r, widget_quark(), WIDGET_ERROR_UNSUPPORTED_ENCODING,
                    "widget '%s' sent non-identity response, cannot embed",
                    widget_path(widget));
        istream_close_unused(body);
        return nullptr;
    }

    content_type = strmap_get_checked(headers, "content-type");

    if (content_type == nullptr ||
        (strncmp(content_type, "text/", 5) != 0 &&
         strncmp(content_type, "application/xhtml+xml", 21) != 0)) {
        g_set_error(error_r, widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                    "widget '%s' sent non-text response",
                    widget_path(widget));
        istream_close_unused(body);
        return nullptr;
    }

    charset = http_header_param(&charset_buffer, content_type, "charset");
    if (charset != nullptr && strref_lower_cmp_literal(charset, "utf-8") != 0 &&
        strref_lower_cmp_literal(charset, "utf8") != 0) {
        /* beng-proxy expects all widgets to send their HTML code in
           utf-8; this widget however used a different charset.
           Automatically convert it with istream_iconv */
        const char *charset2 = strref_dup(pool, charset);
        struct istream *ic = istream_iconv_new(pool, body, "utf-8", charset2);
        if (ic == nullptr) {
            g_set_error(error_r, widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                        "widget '%s' sent unknown charset '%s'",
                        widget_path(widget), charset2);
            istream_close_unused(body);
            return nullptr;
        }

        daemon_log(6, "widget '%s': charset conversion '%s' -> utf-8\n",
                   widget_path(widget), charset2);
        body = ic;

        headers = strmap_dup(pool, headers, 17);
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
                               nullptr);
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
                       struct istream *body, void *ctx)
{
    struct inline_widget *iw = (struct inline_widget *)ctx;

    if (!http_status_is_success(status)) {
        /* the HTTP status code returned by the widget server is
           non-successful - don't embed this widget into the
           template */
        if (body != nullptr)
            istream_close_unused(body);

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_UNSPECIFIED,
                        "response status %d from widget '%s'",
                        status, widget_path(iw->widget));
        inline_widget_close(iw, error);
        return;
    }

    if (body != nullptr) {
        /* check if the content-type is correct for embedding into
           a template, and convert if possible */
        GError *error = nullptr;
        body = widget_response_format(iw->pool, iw->widget,
                                      &headers, body, &error);
        if (body == nullptr) {
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
    struct inline_widget *iw = (struct inline_widget *)ctx;

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

    if (!widget_check_approval(widget)) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_FORBIDDEN,
                        "widget '%s'[class='%s'] is not allowed to embed widget class '%s'",
                        widget_path(widget->parent),
                        widget->parent->class_name,
                        widget->class_name);
        widget_cancel(widget);
        istream_delayed_set_abort(iw->delayed, error);
        return;
    }

    if (!widget_check_host(widget, iw->env->untrusted_host,
                           iw->env->site_name)) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_FORBIDDEN,
                        "untrusted host name mismatch");
        widget_cancel(widget);
        istream_delayed_set_abort(iw->delayed, error);
        return;
    }

    if (!widget_has_default_view(widget)) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_NO_SUCH_VIEW,
                        "No such view: %s", widget->view_name);
        widget_cancel(widget);
        istream_delayed_set_abort(iw->delayed, error);
        return;
    }

    if (widget->session_sync_pending) {
        struct session *session = session_get(iw->env->session_id);
        if (session != nullptr) {
            widget_sync_session(widget, session);
            session_put(session);
        } else
            widget->session_sync_pending = false;
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
    struct inline_widget *iw = (struct inline_widget *)_ctx;

    if (iw->widget->cls != nullptr) {
        inline_widget_set(iw);
    } else {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_UNSPECIFIED,
                        "failed to look up widget class '%s'",
                        iw->widget->class_name);
        widget_cancel(iw->widget);
        inline_widget_close(iw, error);
    }
}


/*
 * Constructor
 *
 */

struct istream *
embed_inline_widget(struct pool *pool, struct processor_env *env,
                    struct widget *widget)
{
    auto iw = NewFromPool<struct inline_widget>(pool);
    struct istream *hold;

    assert(pool != nullptr);
    assert(env != nullptr);
    assert(widget != nullptr);

    if (widget->display == widget::WIDGET_DISPLAY_NONE) {
        widget_cancel(widget);
        return nullptr;
    }

    struct istream *request_body = nullptr;
    if (widget->from_request.body != nullptr) {
        /* use a "paused" stream, to avoid a recursion bug: when
           somebody within this stack frame attempts to read from it,
           and the HTTP server trips on an I/O error, the HTTP request
           gets cancelled, but the event cannot reach this stack
           frame; by preventing reads on the request body, this
           situation is avoided */
        request_body = istream_pause_new(pool, widget->from_request.body);

        /* wrap it in istream_hold, because (most likely) the original
           request body was an istream_hold, too */
        widget->from_request.body = istream_hold_new(pool, request_body);
    }

    iw->pool = pool;
    iw->env = env;
    iw->widget = widget;
    iw->delayed = istream_delayed_new(pool);
    hold = istream_hold_new(pool, iw->delayed);

    if (widget->cls == nullptr)
        widget_resolver_new(pool, env->pool,
                            widget,
                            global_translate_cache,
                            class_lookup_callback, iw,
                            istream_delayed_async_ref(iw->delayed));
    else
        inline_widget_set(iw);

    if (request_body != nullptr)
        istream_pause_resume(request_body);

    return hold;
}
