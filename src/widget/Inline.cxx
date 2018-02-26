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
#include "bp/Global.hxx"
#include "http_util.hxx"
#include "HttpResponseHandler.hxx"
#include "strmap.hxx"
#include "istream_html_escape.hxx"
#include "istream/istream.hxx"
#include "istream/istream_cat.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/istream_iconv.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_pause.hxx"
#include "istream/istream_string.hxx"
#include "istream/TimeoutIstream.hxx"
#include "session.hxx"
#include "pool/pool.hxx"
#include "event/TimerEvent.hxx"
#include "util/Cancellable.hxx"
#include "util/StringCompare.hxx"
#include "util/StringFormat.hxx"
#include "util/Exception.hxx"

#include <assert.h>

const struct timeval inline_widget_header_timeout = {
    .tv_sec = 5,
    .tv_usec = 0,
};

const struct timeval inline_widget_body_timeout = {
    .tv_sec = 10,
    .tv_usec = 0,
};

class InlineWidget final : HttpResponseHandler, Cancellable {
    struct pool &pool;
    struct processor_env &env;
    bool plain_text;
    Widget &widget;

    TimerEvent header_timeout_event;

    DelayedIstreamControl &delayed;

    CancellablePointer cancel_ptr;

public:
    InlineWidget(struct pool &_pool, struct processor_env &_env,
                 bool _plain_text,
                 Widget &_widget,
                 DelayedIstreamControl &_delayed)
        :pool(_pool), env(_env),
         plain_text(_plain_text),
         widget(_widget),
         header_timeout_event(*env.event_loop,
                              BIND_THIS_METHOD(OnHeaderTimeout)),
         delayed(_delayed) {
        delayed.cancel_ptr = *this;
    }

    UnusedIstreamPtr MakeResponse(UnusedIstreamPtr input) noexcept {
        return NewTimeoutIstream(pool, std::move(input),
                                 *env.event_loop,
                                 inline_widget_body_timeout);
    }

    void Start() noexcept;

private:
    void Fail(std::exception_ptr ep) noexcept {
        delayed.SetError(ep);
    }

    void SendRequest();
    void ResolverCallback();

    void OnHeaderTimeout() {
        Cancel();
        Fail(std::make_exception_ptr(std::runtime_error("Header timeout")));
    }

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        UnusedIstreamPtr body) noexcept override;
    void OnHttpError(std::exception_ptr ep) noexcept override;

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override;
};

/**
 * Ensure that a widget has the correct type for embedding it into a
 * HTML/XML document.  Returns nullptr (and closes body) if that is
 * impossible.
 *
 * Throws exception on error.
 */
static UnusedIstreamPtr
widget_response_format(struct pool &pool, const Widget &widget,
                       const StringMap &headers, UnusedIstreamPtr body,
                       bool plain_text)
{
    assert(body);

    const char *p = headers.Get("content-encoding");
    if (p != nullptr && strcmp(p, "identity") != 0)
        throw WidgetError(widget, WidgetErrorCode::UNSUPPORTED_ENCODING,
                          "widget sent non-identity response, cannot embed");

    const char *content_type = headers.Get("content-type");

    if (plain_text) {
        if (content_type == nullptr ||
            !StringStartsWith(content_type, "text/plain"))
            throw WidgetError(widget, WidgetErrorCode::UNSUPPORTED_ENCODING,
                              "widget sent non-text/plain response");

        return body;
    }

    if (content_type == nullptr ||
        (!StringStartsWith(content_type, "text/") &&
         !StringStartsWith(content_type, "application/xml") &&
         !StringStartsWith(content_type, "application/xhtml+xml")))
        throw WidgetError(widget, WidgetErrorCode::UNSUPPORTED_ENCODING,
                          "widget sent non-text response");

    const auto charset = http_header_param(content_type, "charset");
    if (!charset.IsNull() && !charset.EqualsIgnoreCase("utf-8") &&
        !charset.EqualsIgnoreCase("utf8")) {
        /* beng-proxy expects all widgets to send their HTML code in
           utf-8; this widget however used a different charset.
           Automatically convert it with istream_iconv */
        const char *charset2 = p_strdup(pool, charset);
        auto ic = istream_iconv_new(pool, std::move(body), "utf-8", charset2);
        if (!ic)
            throw WidgetError(widget, WidgetErrorCode::UNSUPPORTED_ENCODING,
                              StringFormat<64>("widget sent unknown charset '%s'",
                                               charset2));

        widget.logger(6, "charset conversion '", charset2, "' -> utf-8");
        body = std::move(ic);
    }

    if (StringStartsWith(content_type, "text/") &&
        !StringStartsWith(content_type + 5, "html") &&
        !StringStartsWith(content_type + 5, "xml")) {
        /* convert text to HTML */

        widget.logger(6, "converting text to HTML");

        auto i = istream_html_escape_new(pool, std::move(body));
        body = istream_cat_new(pool,
                               istream_string_new(pool,
                                                  "<pre class=\"beng_text_widget\">"),
                               std::move(i),
                               istream_string_new(pool, "</pre>"));
    }

    return body;
}

/*
 * HTTP response handler
 *
 */

void
InlineWidget::OnHttpResponse(http_status_t status, StringMap &&headers,
                             UnusedIstreamPtr body) noexcept
{
    header_timeout_event.Cancel();

    if (!http_status_is_success(status)) {
        /* the HTTP status code returned by the widget server is
           non-successful - don't embed this widget into the
           template */
        body.Clear();

        WidgetError error(widget, WidgetErrorCode::UNSPECIFIED,
                          StringFormat<64>("response status %d", status));
        Fail(std::make_exception_ptr(error));
        return;
    }

    if (body) {
        /* check if the content-type is correct for embedding into
           a template, and convert if possible */
        try {
            body = widget_response_format(pool, widget,
                                          headers, std::move(body),
                                          plain_text);
        } catch (...) {
            Fail(std::current_exception());
            return;
        }
    } else
        body = istream_null_new(pool);

    delayed.Set(std::move(body));
}

void
InlineWidget::OnHttpError(std::exception_ptr ep) noexcept
{
    header_timeout_event.Cancel();

    Fail(ep);
}

void
InlineWidget::Cancel() noexcept
{
    header_timeout_event.Cancel();

    /* make sure that all widget resources are freed when the request
       is cancelled */
    widget.Cancel();

    cancel_ptr.Cancel();
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
        Fail(std::make_exception_ptr(error));
        return;
    }

    try {
        widget.CheckHost(env.untrusted_host, env.site_name);
    } catch (const std::runtime_error &e) {
        WidgetError error(widget, WidgetErrorCode::FORBIDDEN, "Untrusted host");
        widget.Cancel();
        Fail(NestException(std::current_exception(), error));
        return;
    }

    if (!widget.HasDefaultView()) {
        WidgetError error(widget, WidgetErrorCode::NO_SUCH_VIEW,
                          StringFormat<256>("No such view: %s",
                                            widget.from_template.view_name));
        widget.Cancel();
        Fail(std::make_exception_ptr(error));
        return;
    }

    if (widget.session_sync_pending) {
        auto session = env.GetRealmSession();
        if (session)
            widget.LoadFromSession(*session);
        else
            widget.session_sync_pending = false;
    }

    header_timeout_event.Add(inline_widget_header_timeout);
    widget_http_request(pool, widget, env,
                        *this, cancel_ptr);
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
        Fail(std::make_exception_ptr(error));
    }
}

void
InlineWidget::Start() noexcept
{
    if (widget.cls == nullptr)
        ResolveWidget(pool, widget,
                      *global_translate_cache,
                      BIND_THIS_METHOD(ResolverCallback), cancel_ptr);
    else
        SendRequest();
}


/*
 * Constructor
 *
 */

UnusedIstreamPtr
embed_inline_widget(struct pool &pool, struct processor_env &env,
                    bool plain_text,
                    Widget &widget)
{
    SharedPoolPtr<PauseIstreamControl> pause;
    if (widget.from_request.body) {
        /* use a "paused" stream, to avoid a recursion bug: when
           somebody within this stack frame attempts to read from it,
           and the HTTP server trips on an I/O error, the HTTP request
           gets cancelled, but the event cannot reach this stack
           frame; by preventing reads on the request body, this
           situation is avoided */
        auto _pause = istream_pause_new(&pool,
                                        std::move(widget.from_request.body));
        pause = std::move(_pause.second);

        widget.from_request.body = UnusedHoldIstreamPtr(pool, std::move(_pause.first));
    }

    auto delayed = istream_delayed_new(pool, *env.event_loop);

    auto iw = NewFromPool<InlineWidget>(pool, pool, env, plain_text, widget,
                                        delayed.second);

    UnusedHoldIstreamPtr hold(pool, iw->MakeResponse(std::move(delayed.first)));

    iw->Start();

    if (pause)
        pause->Resume();

    return std::move(hold);
}
