// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Request.hxx"
#include "Widget.hxx"
#include "View.hxx"
#include "Class.hxx"
#include "Context.hxx"
#include "Error.hxx"
#include "LookupHandler.hxx"
#include "http/CommonHeaders.hxx"
#include "http/ResponseHandler.hxx"
#include "FilterStatus.hxx"
#include "bp/ProcessorHeaders.hxx"
#include "bp/XmlProcessor.hxx"
#include "bp/WidgetLookupProcessor.hxx"
#include "bp/CssProcessor.hxx"
#include "bp/TextProcessor.hxx"
#include "bp/session/Lease.hxx"
#include "bp/session/Session.hxx"
#include "http/CommonHeaders.hxx"
#include "http/CookieClient.hxx"
#include "http/rl/ResourceLoader.hxx"
#include "bp/Global.hxx"
#include "translation/Transformation.hxx"
#include "translation/SuffixRegistry.hxx"
#include "translation/AddressSuffixRegistry.hxx"
#include "resource_tag.hxx"
#include "strmap.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/AutoPipeIstream.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "pool/SharedPtr.hxx"
#include "AllocatorPtr.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "util/Cancellable.hxx"
#include "util/StringAPI.hxx"
#include "stopwatch.hxx"

#include <assert.h>
#include <string.h>

class WidgetRequest final
	: PoolLeakDetector, HttpResponseHandler, SuffixRegistryHandler, Cancellable
{
	struct pool &pool;

	const StopwatchPtr parent_stopwatch;

	unsigned num_redirects = 0;

	/**
	 * This attribute remembers the previous status for
	 * ApplyFilterStatus().  Zero means the response was not generated
	 * by a filter.
	 */
	HttpStatus previous_status = {};

	Widget &widget;
	const char *const lookup_id = nullptr;

	SharedPoolPtr<WidgetContext> ctx;
	const char *host_and_port;

	/**
	 * the next transformation to be applied to the widget response
	 */
	IntrusiveForwardList<Transformation> transformations;

	/**
	 * An identifier for the source stream of the current
	 * transformation.  This is used by the filter cache to address
	 * resources.
	 */
	StringWithHash resource_tag{nullptr};

	/**
	 * The Content-Type from the suffix registry.
	 */
	const char *content_type = nullptr;

	WidgetLookupHandler *lookup_handler;
	HttpResponseHandler *http_handler;

	CancellablePointer &caller_cancel_ptr;
	CancellablePointer cancel_ptr;

public:
	WidgetRequest(struct pool &_pool, Widget &_widget,
		      SharedPoolPtr<WidgetContext> &&_ctx,
		      const StopwatchPtr &_parent_stopwatch,
		      HttpResponseHandler &_handler,
		      CancellablePointer &_cancel_ptr) noexcept
		:PoolLeakDetector(_pool),
		 pool(_pool),
		 parent_stopwatch(_parent_stopwatch),
		 widget(_widget), ctx(std::move(_ctx)),
		 http_handler(&_handler),
		 caller_cancel_ptr(_cancel_ptr)
	{
		caller_cancel_ptr = *this;
	}

	WidgetRequest(struct pool &_pool, Widget &_widget,
		      SharedPoolPtr<WidgetContext> &&_ctx,
		      const char *_lookup_id,
		      const StopwatchPtr &_parent_stopwatch,
		      WidgetLookupHandler &_handler,
		      CancellablePointer &_cancel_ptr) noexcept
		:PoolLeakDetector(_pool),
		 pool(_pool),
		 parent_stopwatch(_parent_stopwatch),
		 widget(_widget),
		 lookup_id(_lookup_id),
		 ctx(std::move(_ctx)),
		 lookup_handler(&_handler),
		 caller_cancel_ptr(_cancel_ptr)
	{
		caller_cancel_ptr = *this;
	}

	void Destroy() noexcept {
		DeleteFromPool(pool, this);
	}

	bool ContentTypeLookup() noexcept;
	void SendRequest() noexcept;

private:
	RealmSessionLease GetSessionIfStateful() const {
		return widget.cls->stateful
			? ctx->GetRealmSession()
			: nullptr;
	}

	/**
	 * @param a_view the view that is used to determine the address
	 * @param t_view the view that is used to determine the transformations
	 */
	StringMap MakeRequestHeaders(const WidgetView &a_view,
				     const WidgetView &t_view,
				     bool exclude_host, bool with_body) noexcept;

	bool HandleRedirect(const char *location, UnusedIstreamPtr &body) noexcept;

	void DispatchError(std::exception_ptr ep) noexcept;

	void DispatchError(WidgetErrorCode code, const char *msg) noexcept {
		DispatchError(std::make_exception_ptr(WidgetError(widget, code, msg)));
	}

	/**
	 * A response was received from the widget server; apply
	 * transformations (if enabled) and return it to our handler.
	 * This function will be called (semi-)recursively for every
	 * transformation in the chain.
	 */
	void DispatchResponse(HttpStatus status, StringMap &&headers,
			      UnusedIstreamPtr body) noexcept;

	/**
	 * The widget response is going to be embedded into a template; check
	 * its content type and run the processor (if applicable).
	 */
	void ProcessResponse(HttpStatus status,
			     StringMap &headers, UnusedIstreamPtr body,
			     unsigned options) noexcept;

	void CssProcessResponse(HttpStatus status,
				StringMap &headers, UnusedIstreamPtr body,
				unsigned options) noexcept;

	void TextProcessResponse(HttpStatus status,
				 StringMap &headers,
				 UnusedIstreamPtr body) noexcept;

	void FilterResponse(HttpStatus status,
			    StringMap &&headers, UnusedIstreamPtr body,
			    const FilterTransformation &filter) noexcept;

	/**
	 * Apply a transformation to the widget response and hand it back
	 * to our #HttpResponseHandler implementation.
	 */
	void TransformResponse(HttpStatus status,
			       StringMap &&headers, UnusedIstreamPtr body,
			       const Transformation &t) noexcept;

	/**
	 * Throws exception on error.
	 */
	void UpdateView(StringMap &headers);

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		widget.Cancel();
		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class SuffixRegistryHandler */
	void OnSuffixRegistrySuccess(const char *content_type,
				     bool auto_gzipped, bool auto_brotli_path, bool auto_brotli,
				     const IntrusiveForwardList<Transformation> &transformations) noexcept override;
	void OnSuffixRegistryError(std::exception_ptr ep) noexcept override;
};

StringMap
WidgetRequest::MakeRequestHeaders(const WidgetView &a_view,
				  const WidgetView &t_view,
				  bool exclude_host, bool with_body) noexcept
{
	const AllocatorPtr alloc(pool);

	auto headers =
		ctx->ForwardRequestHeaders(alloc,
					   exclude_host, with_body,
					   widget.from_request.frame && !t_view.HasProcessor(),
					   widget.from_request.frame && t_view.transformations.empty(),
					   widget.from_request.frame && t_view.transformations.empty(),
					   a_view.request_header_forward,
					   host_and_port,
					   widget.GetAddress().GetUriPath());

	if (widget.cls->info_headers) {
		if (widget.id != nullptr)
			headers.Add(alloc, x_cm4all_widget_id_header, widget.id);

		if (widget.class_name != nullptr)
			headers.Add(alloc, x_cm4all_widget_type_header, widget.class_name);

		const char *prefix = widget.GetPrefix();
		if (prefix != nullptr)
			headers.Add(alloc, x_cm4all_widget_prefix_header, prefix);
	}

	if (widget.from_template.headers != nullptr)
		/* copy HTTP request headers from template */
		for (const auto &i : *widget.from_template.headers)
			headers.SecureSet(alloc, alloc.Dup(i.key), alloc.Dup(i.value));

	return headers;
}

bool
WidgetRequest::HandleRedirect(const char *location, UnusedIstreamPtr &body) noexcept
{
	if (num_redirects >= 8)
		return false;

	const WidgetView *view = widget.GetAddressView();
	assert(view != nullptr);

	if (!view->address.IsHttp())
		/* a static or CGI widget cannot send redirects */
		return false;

	const auto p = widget.RelativeUri(pool, true, location);
	if (p.data() == nullptr)
		return false;

	widget.CopyFromRedirectLocation(p, GetSessionIfStateful().get());

	++num_redirects;

	const auto address = widget.GetAddress().Apply(pool, location);
	if (!address.IsDefined())
		return false;

	body.Clear();

	const WidgetView *t_view = widget.GetTransformationView();
	assert(t_view != nullptr);

	ctx->resource_loader.SendRequest(pool,
					 parent_stopwatch,
					 {
						 .sticky_hash = ctx->session_id.GetClusterHash(),
						 .site_name = ctx->site_name,
					 },
					 HttpMethod::GET, address,
					 MakeRequestHeaders(*view, *t_view,
							    address.IsAnyHttp(),
							    false),
					 nullptr,
					 *this,
					 cancel_ptr);

	return true;
}

void
WidgetRequest::DispatchError(std::exception_ptr ep) noexcept
{
	if (lookup_id != nullptr) {
		auto &handler = *lookup_handler;
		Destroy();
		handler.WidgetLookupError(ep);
	} else {
		auto &handler = *http_handler;
		Destroy();
		handler.InvokeError(ep);
	}
}

void
WidgetRequest::ProcessResponse(HttpStatus status,
			       StringMap &headers, UnusedIstreamPtr body,
			       unsigned options) noexcept
{
	if (!body) {
		/* this should not happen, but we're ignoring this formal
		   mistake and pretend everything's alright */
		DispatchResponse(status, processor_header_forward(pool, headers),
				 nullptr);
		return;
	}

	if (!processable(headers)) {
		body.Clear();
		DispatchError(WidgetErrorCode::WRONG_TYPE, "Got non-HTML response");
		return;
	}

	if (lookup_id != nullptr) {
		auto &_pool = pool;
		auto _parent_stopwatch = parent_stopwatch;
		auto &_widget = widget;
		const char *_lookup_id = lookup_id;
		auto _ctx = std::move(ctx);
		auto &_handler = *lookup_handler;
		auto &_cancel_ptr = caller_cancel_ptr;

		Destroy();

		processor_lookup_widget(_pool, _parent_stopwatch, std::move(body),
					_widget, _lookup_id,
					std::move(_ctx), options,
					_handler,
					_cancel_ptr);
	} else
		DispatchResponse(status, processor_header_forward(pool, headers),
				 processor_process(pool, parent_stopwatch,
						   std::move(body),
						   widget, ctx, options));
}

[[gnu::pure]]
static bool
css_processable(const StringMap &headers) noexcept
{
	const char *content_type = headers.Get(content_type_header);
	return content_type != nullptr &&
		strncmp(content_type, "text/css", 8) == 0;
}

void
WidgetRequest::CssProcessResponse(HttpStatus status,
				  StringMap &headers, UnusedIstreamPtr body,
				  unsigned options) noexcept
{
	if (!body) {
		/* this should not happen, but we're ignoring this formal
		   mistake and pretend everything's alright */
		DispatchResponse(status, processor_header_forward(pool, headers),
				 nullptr);
		return;
	}

	if (!css_processable(headers)) {
		body.Clear();
		DispatchError(WidgetErrorCode::WRONG_TYPE, "Got non-CSS response");
		return;
	}

	DispatchResponse(status, processor_header_forward(pool, headers),
			 css_processor(pool, parent_stopwatch,
				       std::move(body), widget, ctx, options));
}

void
WidgetRequest::TextProcessResponse(HttpStatus status,
				   StringMap &headers,
				   UnusedIstreamPtr body) noexcept
{
	if (!body) {
		/* this should not happen, but we're ignoring this formal
		   mistake and pretend everything's alright */
		DispatchResponse(status, processor_header_forward(pool, headers),
				 nullptr);
		return;
	}

	if (!text_processor_allowed(headers)) {
		body.Clear();
		DispatchError(WidgetErrorCode::WRONG_TYPE, "Got non-text response");
		return;
	}

	DispatchResponse(status, processor_header_forward(pool, headers),
			 text_processor(pool, std::move(body), widget, *ctx));
}

void
WidgetRequest::FilterResponse(HttpStatus status,
			      StringMap &&headers, UnusedIstreamPtr body,
			      const FilterTransformation &filter) noexcept
{
	const AllocatorPtr alloc(pool);

	previous_status = status;

	const StringWithHash source_tag = resource_tag_append_etag(alloc, resource_tag, headers);
	resource_tag = !source_tag.IsNull()
		? resource_tag_append_filter(alloc, source_tag, filter.GetId(alloc))
		: StringWithHash{nullptr};

	if (filter.reveal_user)
		forward_reveal_user(alloc, headers, ctx->user);

	if (body)
		body = NewAutoPipeIstream(&pool, std::move(body), global_pipe_stock);

	ctx->filter_resource_loader
		.SendRequest(pool,
			     parent_stopwatch,
			     {
				     .sticky_hash = ctx->session_id.GetClusterHash(),
				     .status = status,
				     .body_etag = source_tag,
				     .cache_tag = filter.cache_tag,
				     .site_name = ctx->site_name,
			     },
			     HttpMethod::POST, filter.address,
			     std::move(headers), std::move(body),
			     *this,
			     cancel_ptr);
}

void
WidgetRequest::TransformResponse(HttpStatus status,
				 StringMap &&headers, UnusedIstreamPtr body,
				 const Transformation &t) noexcept
{
	const char *p = headers.Get(content_encoding_header);
	if (p != nullptr && !StringIsEqual(p, "identity")) {
		body.Clear();
		DispatchError(WidgetErrorCode::UNSUPPORTED_ENCODING,
			      "Got non-identity response, cannot transform");
		return;
	}

	switch (t.type) {
	case Transformation::Type::PROCESS:
		/* processor responses cannot be cached */
		resource_tag = StringWithHash{nullptr};

		ProcessResponse(status, headers, std::move(body),
				t.u.processor.options);
		break;

	case Transformation::Type::PROCESS_CSS:
		/* processor responses cannot be cached */
		resource_tag = StringWithHash{nullptr};

		CssProcessResponse(status, headers, std::move(body),
				   t.u.css_processor.options);
		break;

	case Transformation::Type::PROCESS_TEXT:
		/* processor responses cannot be cached */
		resource_tag = StringWithHash{nullptr};

		TextProcessResponse(status, headers, std::move(body));
		break;

	case Transformation::Type::FILTER:
		FilterResponse(status, std::move(headers), std::move(body),
			       t.u.filter);
		break;
	}
}

static bool
widget_transformation_enabled(const Widget *widget,
			      HttpStatus status)
{
	assert(widget->GetTransformationView() != nullptr);

	return http_status_is_success(status) ||
		(http_status_is_client_error(status) &&
		 widget->GetTransformationView()->filter_4xx);
}

void
WidgetRequest::DispatchResponse(HttpStatus status, StringMap &&headers,
				UnusedIstreamPtr body) noexcept
{
	if (!transformations.empty() && widget_transformation_enabled(&widget, status)) {
		/* transform this response */

		const auto &t = transformations.front();
		transformations.pop_front();

		TransformResponse(status, std::move(headers), std::move(body), t);
	} else if (lookup_id != nullptr) {
		body.Clear();

		auto &handler = *lookup_handler;
		Destroy();

		WidgetError error(WidgetErrorCode::NOT_A_CONTAINER,
				  "Cannot process container widget response");
		handler.WidgetLookupError(std::make_exception_ptr(error));
	} else {
		/* no transformation left */

		auto &handler = *http_handler;
		Destroy();

		/* finally pass the response to our handler */
		handler.InvokeResponse(status, std::move(headers), std::move(body));
	}
}

static void
widget_collect_cookies(CookieJar &jar, const StringMap &headers,
		       const char *host_and_port)
{
	auto r = headers.EqualRange(set_cookie2_header);
	if (r.first == r.second)
		r = headers.EqualRange(set_cookie_header);

	for (auto i = r.first; i != r.second; ++i)
		cookie_jar_set_cookie2(jar, i->value, host_and_port, nullptr);
}

void
WidgetRequest::UpdateView(StringMap &headers)
{
	const char *view_name = headers.Get(x_cm4all_view_header);
	if (view_name != nullptr) {
		/* yes, look it up in the class */

		const WidgetView *view = widget.cls->FindViewByName(view_name);
		if (view == nullptr) {
			/* the view specified in the response header does not
			   exist, bail out */

			throw WidgetError(widget, WidgetErrorCode::NO_SUCH_VIEW,
					  FmtBuffer<256>("No such view: '{}'",
							 view_name));
		}

		/* install the new view */
		transformations = {ShallowCopy{}, view->transformations};
	} else if (widget.from_request.unauthorized_view &&
		   processable(headers) &&
		   !widget.IsContainer()) {
		/* postponed check from proxy_widget_continue(): an
		   unauthorized view was selected, which is only allowed if
		   the output is not processable; if it is, we may expose
		   internal widget parameters */

		throw WidgetError(widget, WidgetErrorCode::FORBIDDEN,
				  FmtBuffer<256>("View '{}' cannot be requested "
						 "because the response is processable",
						 widget.GetTransformationView()->name));
	}
}

void
WidgetRequest::OnHttpResponse(HttpStatus status, StringMap &&headers,
			      UnusedIstreamPtr body) noexcept
{
	if (previous_status != HttpStatus{}) {
		status = ApplyFilterStatus(previous_status, status, !!body);
		previous_status = HttpStatus{};
	}

	if (widget.cls->dump_headers) {
		widget.logger(4, "response headers from widget");

		for (const auto &i : headers)
			widget.logger.Fmt(4, "  {:?}: {:?}", i.key, i.value);
	}

	/* TODO shall the address view or the transformation view be used
	   to control response header forwarding? */
	/* TODO do this after X-CM4all-View was applied */
	const WidgetView *view = widget.GetTransformationView();
	assert(view != nullptr);

	if (view->response_header_forward.IsCookieMangle()) {
		if (host_and_port != nullptr) {
			auto session = ctx->GetRealmSession();
			if (session)
				widget_collect_cookies(session->cookies, headers,
						       host_and_port);
		} else {
#ifndef NDEBUG
			auto r = headers.EqualRange(set_cookie2_header);
			if (r.first == r.second)
				r = headers.EqualRange(set_cookie_header);
			if (r.first != r.second)
				widget.logger(4, "ignoring Set-Cookie from widget: no host");
#endif
		}
	}

	if (http_status_is_redirect(status)) {
		const char *location = headers.Get(location_header);
		if (location != nullptr && HandleRedirect(location, body)) {
			return;
		}
	}

	/* select a new view? */

	try {
		UpdateView(headers);
	} catch (...) {
		body.Clear();

		DispatchError(std::current_exception());
		return;
	}

	if (content_type != nullptr)
		headers.Set(pool, content_type_header, content_type);

	if (widget.session_save_pending &&
	    Transformation::HasProcessor(transformations)) {
		auto session = ctx->GetRealmSession();
		if (session)
			widget.SaveToSession(*session);
	}

	DispatchResponse(status, std::move(headers), std::move(body));
}

void
WidgetRequest::OnHttpError(std::exception_ptr ep) noexcept
{
	DispatchError(ep);
}

void
WidgetRequest::SendRequest() noexcept
{
	const WidgetView *a_view = widget.GetAddressView();
	assert(a_view != nullptr);

	const WidgetView *t_view = widget.GetTransformationView();
	assert(t_view != nullptr);

	host_and_port = widget.cls->cookie_host != nullptr
		? widget.cls->cookie_host
		: a_view->address.GetHostAndPort();
	transformations = {ShallowCopy{}, t_view->transformations};

	const auto &address = widget.GetAddress();

	if (!address.IsDefined()) {
		const char *view_name = widget.from_template.view_name;
		if (view_name == nullptr)
			view_name = "[default]";

		DispatchError(WidgetErrorCode::UNSPECIFIED,
			      FmtBuffer<256>("View '{}' does not have an address",
					     view_name));
		return;
	}

	resource_tag = address.GetId(pool);

	UnusedIstreamPtr request_body(std::move(widget.from_request.body));

	auto headers = MakeRequestHeaders(*a_view, *t_view,
					  address.IsAnyHttp(),
					  request_body);

	if (widget.cls->dump_headers) {
		widget.logger(4, "request headers for widget");

		for (const auto &i : headers)
			widget.logger.Fmt(4, "  {:?}: {:?}", i.key, i.value);
	}

	ctx->resource_loader.SendRequest(pool, parent_stopwatch,
					 {
						 .sticky_hash = ctx->session_id.GetClusterHash(),
						 .address_id = resource_tag,
						 .site_name = ctx->site_name,
					 },
					 widget.from_request.method,
					 address,
					 std::move(headers),
					 std::move(request_body),
					 *this, cancel_ptr);
}

void
WidgetRequest::OnSuffixRegistrySuccess(const char *_content_type,
				       bool, bool, bool,
				       // TODO: apply transformations
				       [[maybe_unused]] const IntrusiveForwardList<Transformation> &_transformations) noexcept
{
	content_type = _content_type;
	SendRequest();
}

void
WidgetRequest::OnSuffixRegistryError(std::exception_ptr ep) noexcept
{
	widget.Cancel();
	DispatchError(ep);
}

bool
WidgetRequest::ContentTypeLookup() noexcept
{
	return suffix_registry_lookup(pool, *global_translation_service,
				      widget.GetAddress(),
				      parent_stopwatch,
				      *this, cancel_ptr);
}

/*
 * constructor
 *
 */

void
widget_http_request(struct pool &pool, Widget &widget,
		    SharedPoolPtr<WidgetContext> ctx,
		    const StopwatchPtr &parent_stopwatch,
		    HttpResponseHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept
{
	assert(widget.cls != nullptr);

	auto embed = NewFromPool<WidgetRequest>(pool, pool, widget,
						std::move(ctx),
						parent_stopwatch,
						handler, cancel_ptr);

	if (!embed->ContentTypeLookup())
		embed->SendRequest();
}

void
widget_http_lookup(struct pool &pool, Widget &widget, const char *id,
		   SharedPoolPtr<WidgetContext> ctx,
		   const StopwatchPtr &parent_stopwatch,
		   WidgetLookupHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept
{
	assert(widget.cls != nullptr);
	assert(id != nullptr);

	auto embed = NewFromPool<WidgetRequest>(pool, pool, widget,
						std::move(ctx), id,
						parent_stopwatch,
						handler, cancel_ptr);

	if (!embed->ContentTypeLookup())
		embed->SendRequest();
}
