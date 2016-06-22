/*
 * Embed a widget.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "inline_widget.hxx"
#include "widget_http.hxx"
#include "widget-quark.h"
#include "penv.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "widget_resolver.hxx"
#include "widget_approval.hxx"
#include "widget_request.hxx"
#include "async.hxx"
#include "bp_global.hxx"
#include "http_util.hxx"
#include "http_response.hxx"
#include "strmap.hxx"
#include "http_response.hxx"
#include "istream_html_escape.hxx"
#include "istream/istream.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_delayed.hxx"
#include "istream/istream_hold.hxx"
#include "istream/istream_iconv.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_pause.hxx"
#include "istream/istream_string.hxx"
#include "istream/TimeoutIstream.hxx"
#include "session.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <assert.h>

const struct timeval inline_widget_timeout = {
    .tv_sec = 10,
    .tv_usec = 0,
};

struct InlineWidget {
    struct pool *pool;
    struct processor_env *env;
    bool plain_text;
    Widget *widget;

    Istream *delayed;

    InlineWidget(struct pool &_pool, struct processor_env &_env,
                 bool _plain_text,
                 Widget &_widget)
        :pool(&_pool), env(&_env),
         plain_text(_plain_text),
         widget(&_widget),
         delayed(istream_delayed_new(pool)) {}
};

static void
inline_widget_close(InlineWidget *iw, GError *error)
{
    istream_delayed_set_abort(*iw->delayed, error);
}

/**
 * Ensure that a widget has the correct type for embedding it into a
 * HTML/XML document.  Returns nullptr (and closes body) if that is
 * impossible.
 */
static Istream *
widget_response_format(struct pool *pool, const Widget *widget,
                       const struct strmap *headers, Istream *body,
                       bool plain_text,
                       GError **error_r)
{
    const char *p, *content_type;

    assert(body != nullptr);

    p = strmap_get_checked(headers, "content-encoding");
    if (p != nullptr && strcmp(p, "identity") != 0) {
        g_set_error(error_r, widget_quark(), WIDGET_ERROR_UNSUPPORTED_ENCODING,
                    "widget '%s' sent non-identity response, cannot embed",
                    widget->GetLogName());
        body->CloseUnused();
        return nullptr;
    }

    content_type = strmap_get_checked(headers, "content-type");

    if (plain_text) {
        if (content_type == nullptr ||
            memcmp(content_type, "text/plain", 10) != 0) {
            g_set_error(error_r, widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                        "widget '%s' sent non-text/plain response",
                        widget->GetLogName());
            body->CloseUnused();
            return nullptr;
        }

        return body;
    }

    if (content_type == nullptr ||
        (strncmp(content_type, "text/", 5) != 0 &&
         strncmp(content_type, "application/xml", 15) != 0 &&
         strncmp(content_type, "application/xhtml+xml", 21) != 0)) {
        g_set_error(error_r, widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                    "widget '%s' sent non-text response",
                    widget->GetLogName());
        body->CloseUnused();
        return nullptr;
    }

    const auto charset = http_header_param(content_type, "charset");
    if (!charset.IsNull() && !charset.EqualsLiteralIgnoreCase("utf-8") &&
        !charset.EqualsLiteralIgnoreCase("utf8")) {
        /* beng-proxy expects all widgets to send their HTML code in
           utf-8; this widget however used a different charset.
           Automatically convert it with istream_iconv */
        const char *charset2 = p_strdup(*pool, charset);
        Istream *ic = istream_iconv_new(pool, *body, "utf-8", charset2);
        if (ic == nullptr) {
            g_set_error(error_r, widget_quark(), WIDGET_ERROR_WRONG_TYPE,
                        "widget '%s' sent unknown charset '%s'",
                        widget->GetLogName(), charset2);
            body->CloseUnused();
            return nullptr;
        }

        daemon_log(6, "widget '%s': charset conversion '%s' -> utf-8\n",
                   widget->GetLogName(), charset2);
        body = ic;
    }

    if (strncmp(content_type, "text/", 5) == 0 &&
        strncmp(content_type + 5, "html", 4) != 0 &&
        strncmp(content_type + 5, "xml", 3) != 0) {
        /* convert text to HTML */

        daemon_log(6, "widget '%s': converting text to HTML\n",
                   widget->GetLogName());

        body = istream_html_escape_new(*pool, *body);
        body = istream_cat_new(*pool,
                               istream_string_new(pool,
                                                  "<pre class=\"beng_text_widget\">"),
                               body,
                               istream_string_new(pool, "</pre>"));
    }

    return body;
}

/*
 * HTTP response handler
 *
 */

static void
inline_widget_response(http_status_t status,
                       struct strmap *headers,
                       Istream *body, void *ctx)
{
    auto *iw = (InlineWidget *)ctx;

    if (!http_status_is_success(status)) {
        /* the HTTP status code returned by the widget server is
           non-successful - don't embed this widget into the
           template */
        if (body != nullptr)
            body->CloseUnused();

        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_UNSPECIFIED,
                        "response status %d from widget '%s'",
                        status, iw->widget->GetLogName());
        inline_widget_close(iw, error);
        return;
    }

    if (body != nullptr) {
        /* check if the content-type is correct for embedding into
           a template, and convert if possible */
        GError *error = nullptr;
        body = widget_response_format(iw->pool, iw->widget,
                                      headers, body, iw->plain_text, &error);
        if (body == nullptr) {
            inline_widget_close(iw, error);
            return;
        }
    } else
        body = istream_null_new(iw->pool);

    istream_delayed_set(*iw->delayed, *body);

    if (iw->delayed->HasHandler())
        iw->delayed->Read();
}

static void
inline_widget_abort(GError *error, void *ctx)
{
    auto *iw = (InlineWidget *)ctx;

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
inline_widget_set(InlineWidget *iw)
{
    const auto &env = *iw->env;
    auto *widget = iw->widget;

    if (!widget_check_approval(widget)) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_FORBIDDEN,
                        "widget '%s' is not allowed to embed widget class '%s'",
                        widget->parent->GetLogName(),
                        widget->class_name);
        widget_cancel(widget);
        istream_delayed_set_abort(*iw->delayed, error);
        return;
    }

    if (!widget->CheckHost(env.untrusted_host, env.site_name)) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_FORBIDDEN,
                        "untrusted host name mismatch in widget '%s'",
                        widget->GetLogName());
        widget_cancel(widget);
        istream_delayed_set_abort(*iw->delayed, error);
        return;
    }

    if (!widget->HasDefaultView()) {
        GError *error =
            g_error_new(widget_quark(), WIDGET_ERROR_NO_SUCH_VIEW,
                        "No such view in widget '%s': %s",
                        widget->GetLogName(),
                        widget->view_name);
        widget_cancel(widget);
        istream_delayed_set_abort(*iw->delayed, error);
        return;
    }

    if (widget->session_sync_pending) {
        auto session = env.GetRealmSession();
        if (session)
            widget_sync_session(*widget, *session);
        else
            widget->session_sync_pending = false;
    }

    widget_http_request(*iw->pool, *iw->widget, *iw->env,
                        inline_widget_response_handler, iw,
                        *istream_delayed_async_ref(*iw->delayed));
}


/*
 * Widget resolver callback
 *
 */

static void
class_lookup_callback(void *_ctx)
{
    auto *iw = (InlineWidget *)_ctx;

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

Istream *
embed_inline_widget(struct pool &pool, struct processor_env &env,
                    bool plain_text,
                    Widget &widget)
{
    Istream *request_body = nullptr;
    if (widget.from_request.body != nullptr) {
        /* use a "paused" stream, to avoid a recursion bug: when
           somebody within this stack frame attempts to read from it,
           and the HTTP server trips on an I/O error, the HTTP request
           gets cancelled, but the event cannot reach this stack
           frame; by preventing reads on the request body, this
           situation is avoided */
        request_body = istream_pause_new(&pool, *widget.from_request.body);

        /* wrap it in istream_hold, because (most likely) the original
           request body was an istream_hold, too */
        widget.from_request.body = istream_hold_new(pool, *request_body);
    }

    auto iw = NewFromPool<InlineWidget>(pool, pool, env, plain_text, widget);

    Istream *timeout = NewTimeoutIstream(pool, *iw->delayed,
                                         *env.event_loop,
                                         inline_widget_timeout);
    Istream *hold = istream_hold_new(pool, *timeout);

    if (widget.cls == nullptr)
        widget_resolver_new(pool, widget,
                            *global_translate_cache,
                            class_lookup_callback, iw,
                            *istream_delayed_async_ref(*iw->delayed));
    else
        inline_widget_set(iw);

    if (request_body != nullptr)
        istream_pause_resume(*request_body);

    return hold;
}
