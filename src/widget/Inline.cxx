// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Inline.hxx"
#include "Request.hxx"
#include "Error.hxx"
#include "Widget.hxx"
#include "Context.hxx"
#include "Resolver.hxx"
#include "http/CommonHeaders.hxx"
#include "http/HeaderUtil.hxx"
#include "http/ResponseHandler.hxx"
#include "strmap.hxx"
#include "escape/HTML.hxx"
#include "escape/Istream.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/istream_null.hxx"
#include "istream/PauseIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/TimeoutIstream.hxx"
#include "bp/session/Lease.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "net/TimeoutError.hxx"
#include "util/Cancellable.hxx"
#include "util/LimitedConcurrencyQueue.hxx"
#include "util/StringAPI.hxx"
#include "util/StringCompare.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

#include <assert.h>

using std::string_view_literals::operator""sv;

static constexpr Event::Duration inline_widget_header_timeout = std::chrono::seconds(5);
const Event::Duration inline_widget_body_timeout = std::chrono::seconds(10);

static LimitedConcurrencyQueue &
GetChildThrottler(EventLoop &event_loop, Widget &widget) noexcept
{
	if (!widget.child_throttler)
		widget.child_throttler = std::make_unique<LimitedConcurrencyQueue>(event_loop, 32);
	return *widget.child_throttler;
}

class InlineWidget final : PoolLeakDetector, HttpResponseHandler, Cancellable {
	struct pool &pool;
	const SharedPoolPtr<WidgetContext> ctx;
	const StopwatchPtr parent_stopwatch;
	const bool plain_text;
	Widget &widget;

	LimitedConcurrencyJob throttle_job;

	CoarseTimerEvent header_timeout_event;

	DelayedIstreamControl &delayed;

	CancellablePointer cancel_ptr;

public:
	InlineWidget(struct pool &_pool, SharedPoolPtr<WidgetContext> &&_ctx,
		     const StopwatchPtr &_parent_stopwatch,
		     bool _plain_text,
		     Widget &_widget,
		     DelayedIstreamControl &_delayed) noexcept
		:PoolLeakDetector(_pool),
		 pool(_pool), ctx(std::move(_ctx)),
		 parent_stopwatch(_parent_stopwatch),
		 plain_text(_plain_text),
		 widget(_widget),
		 throttle_job(GetChildThrottler(ctx->event_loop,
						*widget.parent),
			      BIND_THIS_METHOD(OnThrottled)),
		 header_timeout_event(ctx->event_loop,
				      BIND_THIS_METHOD(OnHeaderTimeout)),
		 delayed(_delayed) {
		delayed.cancel_ptr = *this;
	}

	UnusedIstreamPtr MakeResponse(UnusedIstreamPtr input) noexcept {
		return NewTimeoutIstream(pool, std::move(input),
					 ctx->event_loop,
					 inline_widget_body_timeout);
	}

	void Start() noexcept;

private:
	void Destroy() noexcept {
		DeleteFromPool(pool, this);
	}

	void Fail(std::exception_ptr &&ep) noexcept {
		auto &_delayed = delayed;
		Destroy();
		_delayed.SetError(std::move(ep));
	}

	void SendRequest() noexcept;
	void ResolverCallback() noexcept;

	void OnHeaderTimeout() noexcept {
		widget.Cancel();
		cancel_ptr.Cancel();
		Fail(std::make_exception_ptr(TimeoutError{"Header timeout"}));
	}

	/* LimitedConcurrencyJob callback */
	void OnThrottled() noexcept;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
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

	const char *p = headers.Get(content_encoding_header);
	if (p != nullptr && !StringIsEqual(p, "identity"))
		throw WidgetError(widget, WidgetErrorCode::UNSUPPORTED_ENCODING,
				  "widget sent non-identity response, cannot embed");

	const char *content_type = headers.Get(content_type_header);

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

	if (StringStartsWith(content_type, "text/") &&
	    !StringStartsWith(content_type + 5, "html") &&
	    !StringStartsWith(content_type + 5, "xml")) {
		/* convert text to HTML */

		widget.logger(6, "converting text to HTML");

		auto i = istream_escape_new(pool, std::move(body), html_escape_class);
		body = NewConcatIstream(pool,
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
InlineWidget::OnHttpResponse(HttpStatus status, StringMap &&headers,
			     UnusedIstreamPtr body) noexcept
{
	assert(throttle_job.IsRunning());

	header_timeout_event.Cancel();

	if (!http_status_is_success(status)) {
		/* the HTTP status code returned by the widget server is
		   non-successful - don't embed this widget into the
		   template */
		body.Clear();

		WidgetError error(widget, WidgetErrorCode::UNSPECIFIED,
				  FmtBuffer<64>("response status {}",
						static_cast<unsigned>(status)));
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

		auto &_delayed = delayed;
		Destroy();
		_delayed.Set(std::move(body));
	} else {
		auto &_delayed = delayed;
		Destroy();
		_delayed.SetEof();
	}
}

void
InlineWidget::OnHttpError(std::exception_ptr ep) noexcept
{
	assert(throttle_job.IsRunning());

	header_timeout_event.Cancel();

	Fail(std::move(ep));
}

void
InlineWidget::Cancel() noexcept
{
	header_timeout_event.Cancel();

	/* make sure that all widget resources are freed when the request
	   is cancelled */
	widget.Cancel();

	/* cancel_ptr can be unset if we're waiting for the
	   LimitedConcurrencyJob callback */
	if (cancel_ptr)
		cancel_ptr.Cancel();

	/* the destructor will automatically cancel the
	   LimitedConcurrencyJob */
	Destroy();
}

/*
 * internal
 *
 */

void
InlineWidget::SendRequest() noexcept
try {
	assert(throttle_job.IsRunning());

	widget.CheckApproval();
	widget.CheckHost(ctx->untrusted_host, ctx->site_name);

	if (!widget.HasDefaultView())
		throw WidgetError(widget, WidgetErrorCode::NO_SUCH_VIEW,
				  FmtBuffer<256>("No such view: {}",
						 widget.from_template.view_name));

	if (widget.session_sync_pending) {
		auto session = ctx->GetRealmSession();
		if (session)
			widget.LoadFromSession(*session);
		else
			widget.session_sync_pending = false;
	}

	header_timeout_event.Schedule(inline_widget_header_timeout);
	widget_http_request(pool, widget, ctx,
			    parent_stopwatch,
			    *this, cancel_ptr);
} catch (...) {
	widget.Cancel();
	Fail(std::current_exception());
}


/*
 * Widget resolver callback
 *
 */

void
InlineWidget::ResolverCallback() noexcept
{
	cancel_ptr = nullptr;

	if (widget.cls != nullptr) {
		if (throttle_job.IsRunning())
			SendRequest();
	} else {
		WidgetError error(widget, WidgetErrorCode::UNSPECIFIED,
				  "Failed to look up widget class");
		widget.Cancel();
		Fail(std::make_exception_ptr(error));
	}
}

void
InlineWidget::OnThrottled() noexcept
{
	/* send the HTTP request unless we're still waiting for
	   ResolveWidget() to finish */
	if (widget.cls != nullptr)
		SendRequest();
}

void
InlineWidget::Start() noexcept
{
	/* this check must come before
	   LimitedConcurrencyJob::Schedule(); if it is true, then
	   OnThrottled() will do nothing and this object is not yet
	   destructed */
	const bool need_resolver = widget.cls == nullptr;

	throttle_job.Schedule();

	if (need_resolver)
		ResolveWidget(pool, widget,
			      *ctx->widget_registry,
			      BIND_THIS_METHOD(ResolverCallback), cancel_ptr);
}

/*
 * Constructor
 *
 */

UnusedIstreamPtr
embed_inline_widget(struct pool &pool, SharedPoolPtr<WidgetContext> ctx,
		    const StopwatchPtr &parent_stopwatch,
		    bool plain_text,
		    Widget &widget) noexcept
{
	SharedPoolPtr<PauseIstreamControl> pause;
	if (widget.from_request.body) {
		/* use a "paused" stream, to avoid a recursion bug: when
		   somebody within this stack frame attempts to read from it,
		   and the HTTP server trips on an I/O error, the HTTP request
		   gets cancelled, but the event cannot reach this stack
		   frame; by preventing reads on the request body, this
		   situation is avoided */
		auto _pause = NewPauseIstream(pool, ctx->event_loop,
					      std::move(widget.from_request.body));
		pause = std::move(_pause.second);

		widget.from_request.body = UnusedHoldIstreamPtr(pool, std::move(_pause.first));
	}

	auto delayed = istream_delayed_new(pool, ctx->event_loop);

	auto iw = NewFromPool<InlineWidget>(pool, pool, std::move(ctx),
					    parent_stopwatch,
					    plain_text, widget,
					    delayed.second);

	UnusedHoldIstreamPtr hold(pool, iw->MakeResponse(std::move(delayed.first)));

	iw->Start();

	if (pause)
		pause->Resume();

	return hold;
}
