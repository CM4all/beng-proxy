/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Inline.hxx"
#include "Request.hxx"
#include "Error.hxx"
#include "Widget.hxx"
#include "Class.hxx"
#include "Resolver.hxx"
#include "Approval.hxx"
#include "penv.hxx"
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
#include "util/StringFormat.hxx"
#include "util/Exception.hxx"

#include <assert.h>

const struct timeval inline_widget_timeout = {
    .tv_sec = 10,
    .tv_usec = 0,
};

struct InlineWidget final : HttpResponseHandler {
    struct pool &pool;
    struct processor_env &env;
    bool plain_text;
    Widget &widget;

    Istream *delayed;

    InlineWidget(struct pool &_pool, struct processor_env &_env,
                 bool _plain_text,
                 Widget &_widget)
        :pool(_pool), env(_env),
         plain_text(_plain_text),
         widget(_widget),
         delayed(istream_delayed_new(&pool)) {}

    void SendRequest();
    void ResolverCallback();

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(std::exception_ptr ep) override;
};

static void
inline_widget_close(InlineWidget *iw, std::exception_ptr ep)
{
    istream_delayed_set_abort(*iw->delayed, ep);
}

/**
 * Ensure that a widget has the correct type for embedding it into a
 * HTML/XML document.  Returns nullptr (and closes body) if that is
 * impossible.
 *
 * Throws exception on error.
 */
static Istream *
widget_response_format(struct pool &pool, const Widget &widget,
                       const StringMap &headers, Istream &_body,
                       bool plain_text)
{
    auto *body = &_body;

    assert(body != nullptr);

    const char *p = headers.Get("content-encoding");
    if (p != nullptr && strcmp(p, "identity") != 0) {
        body->CloseUnused();
        throw WidgetError(widget, WidgetErrorCode::UNSUPPORTED_ENCODING,
                          "widget sent non-identity response, cannot embed");
    }

    const char *content_type = headers.Get("content-type");

    if (plain_text) {
        if (content_type == nullptr ||
            memcmp(content_type, "text/plain", 10) != 0) {
            body->CloseUnused();
            throw WidgetError(widget, WidgetErrorCode::UNSUPPORTED_ENCODING,
                              "widget sent non-text/plain response");
        }

        return body;
    }

    if (content_type == nullptr ||
        (strncmp(content_type, "text/", 5) != 0 &&
         strncmp(content_type, "application/xml", 15) != 0 &&
         strncmp(content_type, "application/xhtml+xml", 21) != 0)) {
        body->CloseUnused();
        throw WidgetError(widget, WidgetErrorCode::UNSUPPORTED_ENCODING,
                          "widget sent non-text response");
    }

    const auto charset = http_header_param(content_type, "charset");
    if (!charset.IsNull() && !charset.EqualsLiteralIgnoreCase("utf-8") &&
        !charset.EqualsLiteralIgnoreCase("utf8")) {
        /* beng-proxy expects all widgets to send their HTML code in
           utf-8; this widget however used a different charset.
           Automatically convert it with istream_iconv */
        const char *charset2 = p_strdup(pool, charset);
        Istream *ic = istream_iconv_new(&pool, *body, "utf-8", charset2);
        if (ic == nullptr) {
            body->CloseUnused();
            throw WidgetError(widget, WidgetErrorCode::UNSUPPORTED_ENCODING,
                              StringFormat<64>("widget sent unknown charset '%s'",
                                               charset2));
        }

        widget.logger(6, "charset conversion '", charset2, "' -> utf-8");
        body = ic;
    }

    if (strncmp(content_type, "text/", 5) == 0 &&
        strncmp(content_type + 5, "html", 4) != 0 &&
        strncmp(content_type + 5, "xml", 3) != 0) {
        /* convert text to HTML */

        widget.logger(6, "converting text to HTML");

        body = istream_html_escape_new(pool, *body);
        body = istream_cat_new(pool,
                               istream_string_new(&pool,
                                                  "<pre class=\"beng_text_widget\">"),
                               body,
                               istream_string_new(&pool, "</pre>"));
    }

    return body;
}

/*
 * HTTP response handler
 *
 */

void
InlineWidget::OnHttpResponse(http_status_t status, StringMap &&headers,
                             Istream *body)
{
    if (!http_status_is_success(status)) {
        /* the HTTP status code returned by the widget server is
           non-successful - don't embed this widget into the
           template */
        if (body != nullptr)
            body->CloseUnused();

        WidgetError error(widget, WidgetErrorCode::UNSPECIFIED,
                          StringFormat<64>("response status %d", status));
        inline_widget_close(this, std::make_exception_ptr(error));
        return;
    }

    if (body != nullptr) {
        /* check if the content-type is correct for embedding into
           a template, and convert if possible */
        try {
            body = widget_response_format(pool, widget,
                                          headers, *body, plain_text);
        } catch (...) {
            inline_widget_close(this, std::current_exception());
            return;
        }
    } else
        body = istream_null_new(&pool);

    istream_delayed_set(*delayed, *body);

    if (delayed->HasHandler())
        delayed->Read();
}

void
InlineWidget::OnHttpError(std::exception_ptr ep)
{
    inline_widget_close(this, ep);
}

/*
 * internal
 *
 */

void
InlineWidget::SendRequest()
{
    if (!widget_check_approval(&widget)) {
        WidgetError error(*widget.parent, WidgetErrorCode::FORBIDDEN,
                          StringFormat<256>("not allowed to embed widget class '%s'",
                                            widget.class_name));
        widget.Cancel();
        istream_delayed_set_abort(*delayed, std::make_exception_ptr(error));
        return;
    }

    try {
        widget.CheckHost(env.untrusted_host, env.site_name);
    } catch (const std::runtime_error &e) {
        WidgetError error(widget, WidgetErrorCode::FORBIDDEN, "Untrusted host");
        widget.Cancel();
        istream_delayed_set_abort(*delayed, NestException(std::current_exception(), error));
        return;
    }

    if (!widget.HasDefaultView()) {
        WidgetError error(widget, WidgetErrorCode::NO_SUCH_VIEW,
                          StringFormat<256>("No such view: %s",
                                            widget.from_template.view_name));
        widget.Cancel();
        istream_delayed_set_abort(*delayed, std::make_exception_ptr(error));
        return;
    }

    if (widget.session_sync_pending) {
        auto session = env.GetRealmSession();
        if (session)
            widget.LoadFromSession(*session);
        else
            widget.session_sync_pending = false;
    }

    widget_http_request(pool, widget, env,
                        *this,
                        istream_delayed_cancellable_ptr(*delayed));
}


/*
 * Widget resolver callback
 *
 */

void
InlineWidget::ResolverCallback()
{
    if (widget.cls != nullptr) {
        SendRequest();
    } else {
        WidgetError error(widget, WidgetErrorCode::UNSPECIFIED,
                          "Failed to look up widget class");
        widget.Cancel();
        inline_widget_close(this, std::make_exception_ptr(error));
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
        ResolveWidget(pool, widget,
                      *global_translate_cache,
                      BIND_METHOD(*iw, &InlineWidget::ResolverCallback),
                      istream_delayed_cancellable_ptr(*iw->delayed));
    else
        iw->SendRequest();

    if (request_body != nullptr)
        istream_pause_resume(*request_body);

    return hold;
}
