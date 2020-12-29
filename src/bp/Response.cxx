/*
 * Copyright 2007-2020 CM4all GmbH
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

/*
 * Utilities for transforming the HTTP response being sent.
 */

#include "Request.hxx"
#include "RLogger.hxx"
#include "Connection.hxx"
#include "PendingResponse.hxx"
#include "ProxyWidget.hxx"
#include "Instance.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Headers.hxx"
#include "http/PHeaderUtil.hxx"
#include "http/HeaderWriter.hxx"
#include "ForwardHeaders.hxx"
#include "widget/Widget.hxx"
#include "widget/Ptr.hxx"
#include "widget/Ref.hxx"
#include "widget/Context.hxx"
#include "session/Session.hxx"
#include "GrowingBuffer.hxx"
#include "ResourceLoader.hxx"
#include "resource_tag.hxx"
#include "hostname.hxx"
#include "strmap.hxx"
#include "pheaders.hxx"
#include "XmlProcessor.hxx"
#include "CssProcessor.hxx"
#include "TextProcessor.hxx"
#include "istream/istream_deflate.hxx"
#include "istream/AutoPipeIstream.hxx"
#include "istream/YamlSubstIstream.hxx"
#include "istream/istream_string.hxx"
#include "AllocatorPtr.hxx"
#include "pool/pool.hxx"
#include "translation/Transformation.hxx"
#include "translation/Service.hxx"
#include "http/Address.hxx"
#include "relocate_uri.hxx"
#include "FilterStatus.hxx"

static const char *
request_absolute_uri(const IncomingHttpRequest &request,
		     const char *scheme, const char *host,
		     const char *uri) noexcept
{
	assert(uri != nullptr);

	if (scheme == nullptr)
		scheme = "http";

	if (host == nullptr)
		host = request.headers.Get("host");

	if (host == nullptr || !hostname_is_well_formed(host))
		return nullptr;

	const AllocatorPtr alloc(request.pool);
	return alloc.Concat(scheme, "://", host, uri);
}

/**
 * Drop a widget and all its descendants from the session.
 *
 * @param session a locked session object
 * @param ref the top window to drop; nullptr drops all widgets
 */
static void
session_drop_widgets(RealmSession &session, const char *uri,
		     const WidgetRef *ref) noexcept
{
	WidgetSession::Set *map = &session.widgets;
	const char *id = uri;

	while (true) {
		auto i = map->find(id, WidgetSession::Compare());
		if (i == map->end())
			/* no such widget session */
			return;

		auto &ws = *i;

		if (ref == nullptr) {
			/* found the widget session */
			map->erase(i);
			ws.Destroy(session.parent.pool);
			return;
		}

		map = &ws.children;
		id = ref->id;
		ref = ref->next;
	}
}

inline UnusedIstreamPtr
Request::AutoDeflate(HttpHeaders &response_headers,
		     UnusedIstreamPtr response_body) noexcept
{
	if (compressed) {
		/* already compressed */
	} else if (response_body &&
		   translate.response->auto_deflate &&
		   http_client_accepts_encoding(request.headers, "deflate") &&
		   response_headers.Get("content-encoding") == nullptr) {
		auto available = response_body.GetAvailable(false);
		if (available < 0 || available >= 512) {
			compressed = true;
			response_headers.Write("content-encoding", "deflate");
			response_body = istream_deflate_new(pool, std::move(response_body),
							    instance.event_loop);
		}
	} else if (response_body &&
		   translate.response->auto_gzip &&
		   http_client_accepts_encoding(request.headers, "gzip") &&
		   response_headers.Get("content-encoding") == nullptr) {
		auto available = response_body.GetAvailable(false);
		if (available < 0 || available >= 512) {
			compressed = true;
			response_headers.Write("content-encoding", "gzip");
			response_body = istream_deflate_new(pool, std::move(response_body),
							    instance.event_loop, true);
		}
	}

	return response_body;
}

/*
 * processor invocation
 *
 */

SharedPoolPtr<WidgetContext>
Request::NewWidgetContext() const noexcept
{
	const char *uri = translate.response->uri != nullptr
		? translate.response->uri
		: request.uri;

	auto &rl = *(BpRequestLogger *)request.logger;

	auto ctx = SharedPoolPtr<WidgetContext>::Make
		(pool, instance.event_loop,
		 *instance.cached_resource_loader,
		 *instance.buffered_filter_resource_loader,
		 instance.widget_registry,
		 rl.site_name,
		 translate.response->untrusted,
		 request.local_host_and_port, request.remote_host,
		 uri,
		 request_absolute_uri(request,
				      translate.response->scheme,
				      translate.response->host,
				      uri),
		 dissected_uri.base,
		 &args,
		 session_cookie,
		 session_id, realm,
		 &request.headers);

	ctx->peer_subject = connection.peer_subject;
	ctx->peer_issuer_subject = connection.peer_issuer_subject;
	ctx->user = user;

	return ctx;
}

SharedPoolPtr<WidgetContext>
Request::MakeWidgetContext() noexcept
{
	if (!widget_context)
		widget_context = NewWidgetContext();
	return widget_context;
}

inline void
Request::InvokeXmlProcessor(http_status_t status,
			    StringMap &response_headers,
			    UnusedIstreamPtr response_body,
			    const Transformation &transformation) noexcept
{
	assert(!response_sent);

	if (!response_body) {
		DispatchError(HTTP_STATUS_BAD_GATEWAY,
			      "Empty template cannot be processed");
		return;
	}

	if (!processable(response_headers)) {
		response_body.Clear();
		DispatchError(HTTP_STATUS_BAD_GATEWAY,
			      "Invalid template content type");
		return;
	}

	if (translate.response->uri != nullptr)
		dissected_uri.base = translate.response->uri;

	auto ctx = MakeWidgetContext();
	auto &widget = ctx->AddRootWidget
		(MakeRootWidget(pool,
				translate.response->uri != nullptr
				? translate.response->uri
				: p_strdup(pool, dissected_uri.base)));

	const WidgetRef *focus_ref =
		widget_ref_parse(&pool, args.Remove("focus"));

	const WidgetRef *proxy_ref =
		widget_ref_parse(&pool, args.Get("frame"));

	if (focus_ref != nullptr && proxy_ref != nullptr &&
	    !widget_ref_includes(proxy_ref, focus_ref)) {
		/* the focused widget is not reachable because it is not
		   within the "frame" */

		focus_ref = nullptr;

		if (request_body) {
			logger(4, "discarding non-framed request body");
			request_body.Clear();
		}
	}

	widget.from_request.focus_ref = focus_ref;

	if (proxy_ref != nullptr)
		/* disable all following transformations, because we're doing
		   a direct proxy request to a widget */
		CancelTransformations();

	if (translate.response->untrusted != nullptr && proxy_ref == nullptr) {
		logger(2, "refusing to render template on untrusted domain '",
		       translate.response->untrusted, "'");
		ctx.reset();
		response_body.Clear();
		DispatchError(HTTP_STATUS_FORBIDDEN, "Forbidden");
		return;
	}

	if (focus_ref != nullptr) {
		http_method_t method = request.method;
		if (http_method_is_empty(method) && HasTransformations())
			/* the following transformation may need the processed
			   document to generate its headers, so we should not pass
			   HEAD to the processor */
			method = HTTP_METHOD_GET;

		auto &for_focused = *NewFromPool<Widget::ForFocused>(pool, method,
								     args.Remove("path"),
								     dissected_uri.query,
								     std::move(request_body));
		widget.for_focused = &for_focused;
	}

	/* make sure we have a session */
	{
		auto session = MakeRealmSession();
		if (session) {
			if (widget.from_request.focus_ref == nullptr)
				/* drop the widget session and all descendants if there is
				   no focus */
				session_drop_widgets(*session, widget.id, proxy_ref);
		}
	}

	if (proxy_ref != nullptr) {
		/* the client requests a widget in proxy mode */

		ctx.reset();

		proxy_widget(*this, std::move(response_body),
			     widget, proxy_ref,
			     transformation.u.processor.options);
	} else {
		/* the client requests the whole template */
		response_body = processor_process(pool, stopwatch,
						  std::move(response_body),
						  widget,
						  std::move(ctx),
						  transformation.u.processor.options);
		assert(response_body);

		InvokeResponse(status,
			       processor_header_forward(pool, response_headers),
			       std::move(response_body));
	}
}

gcc_pure
static bool
css_processable(const StringMap &headers) noexcept
{
	const char *content_type = headers.Get("content-type");
	return content_type != nullptr &&
		strncmp(content_type, "text/css", 8) == 0;
}

inline void
Request::InvokeCssProcessor(http_status_t status,
			    StringMap &response_headers,
			    UnusedIstreamPtr response_body,
			    const Transformation &transformation) noexcept
{
	assert(!response_sent);

	if (!response_body) {
		DispatchError(HTTP_STATUS_BAD_GATEWAY,
			      "Empty template cannot be processed");
		return;
	}

	if (!css_processable(response_headers)) {
		response_body.Clear();
		DispatchError(HTTP_STATUS_BAD_GATEWAY,
			      "Invalid template content type");
		return;
	}

	if (translate.response->uri != nullptr)
		dissected_uri.base = translate.response->uri;

	auto ctx = MakeWidgetContext();
	auto &widget = ctx->AddRootWidget(MakeRootWidget(pool,
							 p_strdup(pool,
								  dissected_uri.base)));

	if (translate.response->untrusted != nullptr) {
		logger(2, "refusing to render template on untrusted domain '",
		       translate.response->untrusted, "'");
		response_body.Clear();
		ctx.reset();
		DispatchError(HTTP_STATUS_FORBIDDEN, "Forbidden");
		return;
	}

	response_body = css_processor(pool, stopwatch, std::move(response_body),
				      widget, std::move(ctx),
				      transformation.u.css_processor.options);
	assert(response_body);

	InvokeResponse(status,
		       processor_header_forward(pool,
						response_headers),
		       std::move(response_body));
}

inline void
Request::InvokeTextProcessor(http_status_t status,
			     StringMap &response_headers,
			     UnusedIstreamPtr response_body) noexcept
{
	assert(!response_sent);

	if (!response_body) {
		DispatchError(HTTP_STATUS_BAD_GATEWAY,
			      "Empty template cannot be processed");
		return;
	}

	if (!text_processor_allowed(response_headers)) {
		response_body.Clear();
		DispatchError(HTTP_STATUS_BAD_GATEWAY,
			      "Invalid template content type");
		return;
	}

	if (translate.response->uri != nullptr)
		dissected_uri.base = translate.response->uri;

	auto ctx = MakeWidgetContext();
	auto &widget = ctx->AddRootWidget(MakeRootWidget(pool,
							 p_strdup(pool,
								  dissected_uri.base)));

	if (translate.response->untrusted != nullptr) {
		logger(2, "refusing to render template on untrusted domain '",
		       translate.response->untrusted, "'");
		response_body.Clear();
		ctx.reset();
		DispatchError(HTTP_STATUS_FORBIDDEN, "Forbidden");
		return;
	}

	response_body = text_processor(pool, std::move(response_body),
				       widget, *ctx);
	assert(response_body);

	ctx.reset();

	InvokeResponse(status,
		       processor_header_forward(pool, response_headers),
		       std::move(response_body));
}

inline void
Request::InvokeSubst(http_status_t status,
		     StringMap &&response_headers,
		     UnusedIstreamPtr response_body,
		     bool alt_syntax,
		     const char *prefix,
		     const char *yaml_file,
		     const char *yaml_map_path) noexcept
{
	try {
#ifdef HAVE_YAML
		InvokeResponse(status, std::move(response_headers),
			       NewYamlSubstIstream(pool, std::move(response_body),
						   alt_syntax,
						   prefix, yaml_file, yaml_map_path));
#else
		(void)status;
		(void)response_headers;
		(void)response_body;
		(void)alt_syntax;
		(void)prefix;
		(void)yaml_file;
		(void)yaml_map_path;
		throw std::runtime_error("YAML support is disabled");
#endif
	} catch (...) {
		LogDispatchError(std::current_exception());
	}
}

/**
 * Append response headers set by the translation server.
 */
static void
translation_response_headers(HttpHeaders &headers,
			     const TranslateResponse &tr) noexcept
{
	if (tr.response_header_forward[BengProxy::HeaderGroup::AUTH] == BengProxy::HeaderForwardMode::MANGLE) {
		if (tr.www_authenticate != nullptr)
			headers.Write("www-authenticate", tr.www_authenticate);

		if (tr.authentication_info != nullptr)
			headers.Write("authentication-info", tr.authentication_info);
	}

	for (const auto &i : tr.response_headers)
		headers.Write(i.key, i.value);
}

inline void
Request::MoreResponseHeaders(HttpHeaders &headers) const noexcept
{
	/* RFC 2616 3.8: Product Tokens */
	if (product_token != nullptr)
		headers.Write("server", product_token);
	else
		headers.generate_server_header = true;

	/* RFC 2616 14.18: Date */
	if (date != nullptr)
		headers.Write("date", date);
#ifndef NO_DATE_HEADER
	else
		headers.generate_date_header = true;
#endif

	translation_response_headers(headers, *translate.response);
}

inline void
Request::GenerateSetCookie(GrowingBuffer &headers) noexcept
{
	assert(!stateless);
	assert(session_cookie != nullptr);

	if (send_session_cookie) {
		header_write_begin(headers, "set-cookie");
		headers.Write(session_cookie);
		headers.Write("=", 1);
		headers.Write(session_id.Format());
		headers.Write("; HttpOnly; Path=");

		const char *cookie_path = translate.response->cookie_path;
		if (cookie_path == nullptr)
			cookie_path = "/";

		headers.Write(cookie_path);
		headers.Write("; Version=1");

		if (translate.response->secure_cookie)
			headers.Write("; Secure");

		using SS = BpConfig::SessionCookieSameSite;
		switch (connection.config.session_cookie_same_site) {
		case SS::NONE:
			break;

		case SS::STRICT:
			headers.Write("; SameSite=strict");
			break;

		case SS::LAX:
			headers.Write("; SameSite=lax");
			break;
		}

		if (translate.response->cookie_domain != nullptr) {
			headers.Write("; Domain=\"");
			headers.Write(translate.response->cookie_domain);
			headers.Write("\"");
		}

		/* "Discard" must be last, to work around an Android bug*/
		headers.Write("; Discard");

		header_write_finish(headers);

		/* workaround for IE10 bug; see
		   http://projects.intern.cm-ag/view.php?id=3789 for
		   details */
		header_write(headers, "p3p", "CP=\"CAO PSA OUR\"");

		auto session = MakeSession();
		if (session)
			session->cookie_sent = true;
	} else if (translate.response->discard_session &&
		   !session_id.IsDefined()) {
		/* delete the cookie for the discarded session */
		header_write_begin(headers, "set-cookie");
		headers.Write(session_cookie);
		headers.Write("=; HttpOnly; Path=");

		const char *cookie_path = translate.response->cookie_path;
		if (cookie_path == nullptr)
			cookie_path = "/";

		headers.Write(cookie_path);
		headers.Write("; Version=1; Max-Age=0");

		if (translate.response->cookie_domain != nullptr) {
			headers.Write("; Domain=\"");
			headers.Write(translate.response->cookie_domain);
			headers.Write("\"");
		}

		/* "Discard" must be last, to work around an Android bug*/
		headers.Write("; Discard");

		header_write_finish(headers);
	}
}

/*
 * dispatch
 *
 */

inline void
Request::DispatchResponseDirect(http_status_t status, HttpHeaders &&headers,
				UnusedIstreamPtr body) noexcept
{
	assert(!response_sent);

	if (http_status_is_success(status) &&
	    translate.response->www_authenticate != nullptr)
		/* default to "401 Unauthorized" */
		status = HTTP_STATUS_UNAUTHORIZED;

	MoreResponseHeaders(headers);

	DiscardRequestBody();

	if (!stateless)
		GenerateSetCookie(headers.GetBuffer());

	if (translate.response->send_csrf_token &&
	    http_status_is_success(status)) {
		if (headers.Get("access-control-allow-origin") != nullptr) {
			/* if this CORS header indicates that other origins may
			   send requests, then this undermindes our CSRF
			   protection; thus, enabling both CORS headers and
			   SEND_CSRF_TOKEN is a bug */
			DispatchError(HTTP_STATUS_BAD_GATEWAY,
				      "Conflicting CSRF/CORS configuration");
			return;
		}

		WriteCsrfToken(headers);
	}

	if (body)
		body = NewAutoPipeIstream(&pool, std::move(body), instance.pipe_stock);

#ifndef NDEBUG
	response_sent = true;
#endif

	auto &_request = request;
	Destroy();
	_request.SendResponse(status, std::move(headers), std::move(body));
}

inline void
Request::ApplyFilter(http_status_t status, StringMap &&headers2,
		     UnusedIstreamPtr body,
		     const FilterTransformation &filter) noexcept
{
	assert(!pending_filter_response);

	const AllocatorPtr alloc(pool);

	previous_status = status;

	const char *source_tag = resource_tag_append_etag(pool, resource_tag,
							  headers2);
	resource_tag = source_tag != nullptr
		? alloc.Concat(source_tag, '|', filter.GetId(AllocatorPtr(pool)))
		: nullptr;

	if (filter.reveal_user)
		forward_reveal_user(pool, headers2, user);

	if (filter.no_body)
		pending_filter_response =
			UniquePoolPtr<PendingResponse>::Make(pool, status,
							     StringMap{ShallowCopy{}, pool, headers2},
							     UnusedHoldIstreamPtr{pool, std::move(body)});

	if (body)
		body = NewAutoPipeIstream(&pool, std::move(body), instance.pipe_stock);

	instance.buffered_filter_resource_loader
		->SendRequest(pool, stopwatch,
			      session_id.GetClusterHash(),
			      filter.cache_tag,
			      translate.response->site,
			      HTTP_METHOD_POST, filter.address,
			      status, std::move(headers2),
			      std::move(body), source_tag,
			      *this, cancel_ptr);
}

void
Request::ApplyTransformation(http_status_t status, StringMap &&headers,
			     UnusedIstreamPtr response_body,
			     const Transformation &transformation) noexcept
{
	transformed = true;

	switch (transformation.type) {
	case Transformation::Type::FILTER:
		ApplyFilter(status, std::move(headers),
			    std::move(response_body),
			    transformation.u.filter);
		break;

	case Transformation::Type::PROCESS:
		/* processor responses cannot be cached */
		resource_tag = nullptr;

		InvokeXmlProcessor(status, headers, std::move(response_body),
				   transformation);
		break;

	case Transformation::Type::PROCESS_CSS:
		/* processor responses cannot be cached */
		resource_tag = nullptr;

		InvokeCssProcessor(status, headers, std::move(response_body),
				   transformation);
		break;

	case Transformation::Type::PROCESS_TEXT:
		/* processor responses cannot be cached */
		resource_tag = nullptr;

		InvokeTextProcessor(status, headers, std::move(response_body));
		break;

	case Transformation::Type::SUBST:
		/* subst responses cannot be cached */
		resource_tag = nullptr;

		InvokeSubst(status, std::move(headers), std::move(response_body),
			    translate.response->subst_alt_syntax,
			    transformation.u.subst.prefix,
			    transformation.u.subst.yaml_file,
			    transformation.u.subst.yaml_map_path);
		break;
	}
}

gcc_pure
static bool
filter_enabled(const TranslateResponse &tr,
	       http_status_t status) noexcept
{
	return http_status_is_success(status) ||
		(http_status_is_client_error(status) && tr.filter_4xx);
}

void
Request::DispatchResponse(http_status_t status, HttpHeaders &&headers,
			  UnusedIstreamPtr response_body) noexcept
{
	assert(!response_sent);

	if (http_status_is_error(status) && !transformed &&
	    !translate.response->error_document.IsNull()) {
		transformed = true;

		/* for sure, the errdoc library doesn't use the request body;
		   discard it as early as possible */
		DiscardRequestBody();

		DisaptchErrdocResponse(status,
				       translate.response->error_document,
				       std::move(headers),
				       std::move(response_body));
		return;
	}

	/* if HTTP status code is not successful: don't apply
	   transformation on the error document */
	const Transformation *transformation = PopTransformation();
	if (transformation != nullptr &&
	    filter_enabled(*translate.response, status)) {
		ApplyTransformation(status, std::move(headers).ToMap(pool),
				    std::move(response_body),
				    *transformation);
	} else if (!translate.chain.IsNull()) {
		assert(!pending_chain_response);

		pending_chain_response =
			UniquePoolPtr<PendingResponse>::Make(pool, status,
							     std::move(headers),
							     UnusedHoldIstreamPtr{pool, std::move(response_body)});

		TranslateRequest chain_request;
		chain_request.chain = std::exchange(translate.chain, nullptr);
		chain_request.chain_header =
			std::exchange(translate.chain_header, nullptr);
		chain_request.status = status;

		GetTranslationService().SendRequest(pool, chain_request,
						    stopwatch,
						    *this,
						    cancel_ptr);
	} else {
		response_body = AutoDeflate(headers, std::move(response_body));
		DispatchResponseDirect(status, std::move(headers),
				       std::move(response_body));
	}
}

void
Request::DispatchError(http_status_t status, HttpHeaders &&headers,
		       UnusedIstreamPtr response_body) noexcept
{
	/* error responses generated by beng-proxy shall not be
	   transformed */
	CancelTransformations();
	translate.chain = nullptr;

	DispatchResponse(status, std::move(headers),
			 std::move(response_body));
}

void
Request::DispatchError(http_status_t status,
		       HttpHeaders &&headers, const char *msg) noexcept
{
	assert(http_status_is_valid(status));
	assert(msg != nullptr);

	headers.Write("content-type", "text/plain");

	DispatchError(status, std::move(headers),
		      istream_string_new(pool, msg));
}

void
Request::DispatchError(http_status_t status, const char *msg) noexcept
{
	DispatchError(status, {}, msg);
}

void
Request::DispatchRedirect(http_status_t status,
			  const char *location, const char *msg) noexcept
{
	assert(status >= 300 && status < 400);
	assert(location != nullptr);

	if (msg == nullptr)
		msg = "redirection";

	HttpHeaders headers;
	headers.Write("location", location);

	DispatchError(status, std::move(headers), msg);
}

const char *
Request::RelocateCallback(const char *const uri, void *ctx) noexcept
{
	auto &request = *(Request *)ctx;
	auto &tr = *request.translate.response;

	if (tr.base == nullptr || tr.IsExpandable() || !tr.address.IsHttp())
		return uri;

	const char *external_scheme = request.GetExternalUriScheme(tr);
	const char *external_host = request.GetExternalUriHost(tr);

	const auto &address = tr.address.GetHttp();

	StringView internal_path = address.path;
	const char *q = internal_path.Find('?');
	if (q != nullptr)
		/* truncate the query string, because it's not part of
		   request.uri.base either */
		internal_path.size = q - internal_path.data;

	const char *new_uri = RelocateUri(AllocatorPtr{request.request.pool}, uri,
					  address.host_and_port,
					  internal_path,
					  external_scheme, external_host,
					  request.dissected_uri.base, tr.base);
	if (new_uri == nullptr)
		return uri;

	// TODO: check regex and inverse_regex

	return new_uri;
}

/*
 * HTTP response handler
 *
 */

void
Request::OnHttpResponse(http_status_t status, StringMap &&_headers,
			UnusedIstreamPtr body) noexcept
{
	assert(!response_sent);

	/* move the StringMap rvalue reference to the stack to avoid
	   use-after-free bugs when pending_filter_response gets moved
	   into it, which, by closing the given response body, may
	   free the pool where the StringMap parameter points to */
	auto headers = std::move(_headers);

	if (pending_filter_response) {
		/* this is the response of a filter with
		   TranslationCommand::FILTER_NO_BODY */
		if (http_status_is_success(status)) {
			/* discard successful responses and use the
			   pending response */
			status = pending_filter_response->status;
			headers = std::move(pending_filter_response->headers).ToMap(pool);
			body = std::move(pending_filter_response->body);
		} else {
			/* unsucessful response: use that one and
			   discard the pending response */
		}

		pending_filter_response.reset();
	}

	if (previous_status != http_status_t(0)) {
		status = ApplyFilterStatus(previous_status, status, !!body);
		previous_status = http_status_t(0);
	}

	if (collect_cookies) {
		collect_cookies = false;
		CollectCookies(headers);
	}

	if (translate.chain != nullptr)
		/* get the X-CM4all-Chain header from the unfiltered
		   response headers, to be sent to the translation
		   server later when CHAIN is evaluated */
		translate.chain_header = headers.Remove("x-cm4all-chain");

	if (http_status_is_success(status)) {
		using namespace BengProxy;

		if (!transformed &&
		    (translate.response->response_header_forward[HeaderGroup::TRANSFORMATION] == HeaderForwardMode::MANGLE)) {
			/* handle the response header "x-cm4all-view" */
			const char *view_name = headers.Get("x-cm4all-view");
			if (view_name != nullptr) {
				const WidgetView *view =
					widget_view_lookup(translate.response->views, view_name);
				if (view == nullptr) {
					/* the view specified in the response header does not
					   exist, bail out */

					body.Clear();

					logger(4, "No such view: ", view_name);
					DispatchError(HTTP_STATUS_NOT_FOUND, "No such view");
					return;
				}

				translate.transformations = {ShallowCopy{}, view->transformations};
			}
		}

		const Transformation *transformation = PopTransformation();
		if (transformation != nullptr) {
			ApplyTransformation(status, std::move(headers), std::move(body),
					    *transformation);
			return;
		}
	}

	auto new_headers = ForwardResponseHeaders(status, headers,
						  RelocateCallback, this,
						  translate.response->response_header_forward);

	HttpHeaders headers2(std::move(new_headers));

	if (request.method == HTTP_METHOD_HEAD)
		/* pass Content-Length, even though there is no response body
		   (RFC 2616 14.13) */
		headers2.CopyToBuffer(headers, "content-length");

	DispatchResponse(status, std::move(headers2), std::move(body));
}

void
Request::OnHttpError(std::exception_ptr ep) noexcept
{
	assert(!response_sent);

	LogDispatchError(ep);
}
