// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Request.hxx"
#include "PendingResponse.hxx"
#include "Connection.hxx"
#include "Listener.hxx"
#include "Config.hxx"
#include "RLogger.hxx"
#include "Instance.hxx"
#include "PerSite.hxx"
#include "file/Address.hxx"
#include "cgi/Address.hxx"
#include "session/Lease.hxx"
#include "session/Session.hxx"
#include "ExternalSession.hxx"
#include "translation/AddressSuffixRegistry.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Method.hxx"
#include "http/rl/ResourceLoader.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_string.hxx"
#include "AllocatorPtr.hxx"
#include "uri/Args.hxx"
#include "uri/PEdit.hxx"
#include "uri/PEscape.hxx"
#include "uri/Recompose.hxx"
#include "uri/RedirectHttps.hxx"
#include "uri/Verify.hxx"
#include "strmap.hxx"
#include "translation/Service.hxx"
#include "translation/Protocol.hxx"
#include "translation/Layout.hxx"
#include "time/Cast.hxx" // for ToFloatSeconds()
#include "util/SpanCast.hxx"
#include "util/StringCompare.hxx"
#include "HttpMessageResponse.hxx"

#include <assert.h>
#include <fcntl.h> // for AT_*
#include <sys/stat.h>

using std::string_view_literals::operator""sv;

static unsigned translation_protocol_version;
static bool translation_protocol_version_received = false;

static const char *
GetBounceUri(AllocatorPtr alloc, const IncomingHttpRequest &request,
	     const char *scheme, const char *host,
	     const DissectedUri &dissected_uri,
	     const TranslateResponse &response) noexcept
{
	const char *uri_path = request.uri;

	if (response.uri != nullptr) {
		auto new_du = dissected_uri;
		new_du.base = response.uri;
		new_du.path_info = {};
		uri_path = RecomposeUri(alloc, new_du);
	}

	const char *current_uri = alloc.Concat(scheme, "://", host, uri_path);
	const char *escaped_uri = uri_escape_dup(alloc, current_uri);

	return alloc.Concat(response.bounce, escaped_uri);
}

/**
 * Apply session-specific data from the #TranslateResponse.  Returns
 * the session object or nullptr.
 */
inline RealmSessionLease
Request::ApplyTranslateResponseSession(const TranslateResponse &response) noexcept
{
	ApplyTranslateRealm(response, {});

	return ApplyTranslateSession(response);
}

inline void
Request::HandleAddress(const ResourceAddress &address)
{
	assert(address.IsDefined());

	switch (address.type) {
	case ResourceAddress::Type::LOCAL:
		HandleFileAddress(address.GetFile());
		break;

	default:
		HandleProxyAddress();
	}
}

static constexpr TokenBucketConfig
ToTokenBucketConfig(const TranslateTokenBucketParams &src) noexcept
{
	return {
		.rate = static_cast<double>(src.rate),
		.burst = static_cast<double>(src.burst),
	};
}

void
Request::HandleTranslatedRequest2(const TranslateResponse &response) noexcept
{
	if (!response.views.empty())
		translate.transformations = {ShallowCopy{}, response.views.front().transformations};
	else
		translate.transformations.clear();

	translate.chain = response.chain;
	if (translate.chain.data() != nullptr && ++translate.n_chain > 4) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Too many consecutive CHAIN packets", 1);
		return;
	}

	if (response.discard_query_string && dissected_uri.query.data() != nullptr) {
		dissected_uri.query = {};
		request.uri = RecomposeUri(*request.pool, dissected_uri);
	}

	using namespace BengProxy;
	if ((response.request_header_forward[HeaderGroup::COOKIE] != HeaderForwardMode::MANGLE &&
	     response.request_header_forward[HeaderGroup::COOKIE] != HeaderForwardMode::BOTH) ||
	    (response.response_header_forward[HeaderGroup::COOKIE] != HeaderForwardMode::MANGLE &&
	     response.response_header_forward[HeaderGroup::COOKIE] != HeaderForwardMode::BOTH)) {
		/* disable session management if cookies are not mangled by
		   beng-proxy */
		MakeStateless();
	}

	if (response.site != nullptr) {
		auto &rl = *(BpRequestLogger *)request.logger;
		rl.site_name = response.site;
	}

	if (response.analytics_id != nullptr) {
		auto &rl = *(BpRequestLogger *)request.logger;
		rl.analytics_id = response.analytics_id;
	}

	if (response.stats_tag != nullptr) {
		auto &rl = *(BpRequestLogger *)request.logger;
		rl.stats_tag = response.stats_tag;
	}

	if (response.rate_limit_site_requests.IsDefined() ||
	    response.rate_limit_site_traffic.IsDefined()) {
		assert(response.site != nullptr);

		auto per_site = instance.MakePerSite(response.site);

		const auto float_now = ToFloatSeconds(instance.event_loop.SteadyNow().time_since_epoch());

		if (response.rate_limit_site_requests.IsDefined() &&
		    !per_site->CheckRequestCount(ToTokenBucketConfig(response.rate_limit_site_requests), float_now)) {
			DispatchError(HttpStatus::TOO_MANY_REQUESTS);
			return;
		}

		if (response.rate_limit_site_traffic.IsDefined()) {
			if (!per_site->CheckRequestTraffic(float_now)) {
				DispatchError(HttpStatus::TOO_MANY_REQUESTS);
				return;
			}

			/* the "per_site" lease is moved to the
			   BpRequestLogger; it is needed there to
			   update the TokenBucket after the traffic
			   amount of this request is known */
			auto &rl = *(BpRequestLogger *)request.logger;
			rl.per_site = std::move(per_site);
			rl.rate_limit_site_traffic = ToTokenBucketConfig(response.rate_limit_site_traffic);
		}
	}

	{
		auto session = ApplyTranslateResponseSession(response);

		/* always enforce sessions when the processor is enabled */
		if (IsProcessorEnabled() && !session)
			session = MakeRealmSession();

		if (session)
			RefreshExternalSession(connection.instance,
					       session->parent);
	}

	if (translate.address.IsDefined()) {
		HandleAddress(translate.address);
	} else if (CheckHandleRedirectBounceStatus(response)) {
		/* done */
	} else if (response.www_authenticate != nullptr &&
		   /* disable the deprecated HTTP-auth if the new
		      HTTP_AUTH is enabled: */
		   response.http_auth.data() == nullptr) {
		DispatchError(HttpStatus::UNAUTHORIZED);
	} else if (response.break_chain) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "BREAK_CHAIN without CHAIN", 1);
	} else {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Empty response from configuration server", 1);
	}
}

inline const char *
Request::CheckRedirectUri(const TranslateResponse &response) const noexcept
{
	if (response.redirect == nullptr)
		return nullptr;

	const AllocatorPtr alloc(pool);

	const char *redirect_uri = response.redirect;

	if (response.redirect_full_uri &&
	    dissected_uri.args.data() != nullptr)
		redirect_uri = alloc.Concat(redirect_uri, ';',
					    dissected_uri.args,
					    dissected_uri.path_info);

	if (response.redirect_query_string &&
	    dissected_uri.query.data() != nullptr)
		redirect_uri = uri_append_query_string_n(alloc, redirect_uri,
							 dissected_uri.query);

	return redirect_uri;
}

inline const char *
Request::CheckBounceUri(const TranslateResponse &response) const noexcept
{
	if (response.bounce == nullptr)
		return nullptr;

	return GetBounceUri(pool, request,
			    GetExternalUriScheme(response),
			    GetExternalUriHost(response),
			    dissected_uri, response);
}

UniquePoolPtr<PendingResponse>
Request::CheckRedirectBounceStatus(const TranslateResponse &response) noexcept
{
	if (response.redirect == nullptr && response.bounce == nullptr &&
	    response.status == HttpStatus{} && !response.tiny_image &&
	    response.message == nullptr)
		return nullptr;

	HttpStatus status = response.status;
	HttpHeaders headers;
	UnusedIstreamPtr body;

	if (response.tiny_image) {
		headers.Write("content-type", "image/gif");

		static constexpr std::string_view tiny_gif =
			"GIF89a\x01\x00\x01\x00\x80\xff\x00\xff\xff\xff\x00\x00\x00,\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02\x44\x01\x00;"sv;
		body = istream_memory_new(pool, AsBytes(tiny_gif));
	}

	const char *message = response.message;

	const char *redirect_uri = CheckRedirectUri(response);
	if (redirect_uri == nullptr)
		redirect_uri = CheckBounceUri(response);
	if (redirect_uri != nullptr) {
		if (status == HttpStatus{})
			status = HttpStatus::SEE_OTHER;

		headers.Write("location", redirect_uri);

		if (message == nullptr)
			message = "redirection";
	}

	if (message != nullptr && !body) {
		headers.Write("content-type", "text/plain");
		body = istream_string_new(pool, message);
	}

	if (status == HttpStatus{})
		status = body ? HttpStatus::OK : HttpStatus::NO_CONTENT;

	return UniquePoolPtr<PendingResponse>::Make
		(pool, status, std::move(headers), UnusedHoldIstreamPtr{pool, std::move(body)});
}

bool
Request::CheckHandleRedirectBounceStatus(const TranslateResponse &response) noexcept
{
	auto r = CheckRedirectBounceStatus(response);
	if (!r)
		return false;

	CancelChainAndTransformations();
	DispatchResponse(std::move(r));
	return true;
}

[[gnu::pure]]
static bool
ProbeOnePathSuffix(const char *prefix, const char *suffix) noexcept
{
	const size_t prefix_length = strlen(prefix);
	const size_t suffix_length = strlen(suffix);

	char path[PATH_MAX];
	if (prefix_length + suffix_length >= sizeof(path))
		/* path too long */
		return false;

	memcpy(path, prefix, prefix_length);
	memcpy(path + prefix_length, suffix, suffix_length);
	path[prefix_length + suffix_length] = 0;

	struct statx stx;
	return statx(-1, path, AT_STATX_DONT_SYNC, STATX_TYPE, &stx) == 0 &&
		S_ISREG(stx.stx_mode);
}

[[gnu::pure]]
static const char *
ProbePathSuffixes(const char *prefix,
		  const std::span<const char *const> suffixes) noexcept
{
	assert(suffixes.data() != nullptr);
	assert(!suffixes.empty());

	for (const char *current_suffix : suffixes) {
		if (ProbeOnePathSuffix(prefix, current_suffix))
			return current_suffix;
	}

	return nullptr;
}

inline bool
Request::CheckHandleProbePathSuffixes(const TranslateResponse &response) noexcept
{
	if (response.probe_path_suffixes.data() == nullptr)
		return false;

	if (++translate.n_probe_path_suffixes > 2) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Too many consecutive PROBE_PATH_SUFFIXES packets",
				 1);
		return true;
	}

	assert(response.test_path != nullptr);
	const char *prefix = response.test_path;

	const char *found = ProbePathSuffixes(prefix, response.probe_suffixes);

	translate.request.probe_path_suffixes = response.probe_path_suffixes;
	translate.request.probe_suffix = found;
	SubmitTranslateRequest();
	return true;
}

void
Request::OnSuffixRegistrySuccess(const char *content_type,
				 bool auto_gzipped, bool auto_brotli_path, bool auto_brotli,
				 const IntrusiveForwardList<Transformation> &transformations) noexcept
{
	translate.content_type = content_type;
	translate.suffix_transformations = {ShallowCopy{}, transformations};
	translate.auto_gzipped = auto_gzipped;
#ifdef HAVE_BROTLI
	translate.auto_brotli_path = auto_brotli_path;
	translate.auto_brotli = auto_brotli;
#else
	(void)auto_brotli_path;
	(void)auto_brotli;
#endif

	HandleTranslatedRequest2(*translate.response);
}

void
Request::OnSuffixRegistryError(std::exception_ptr ep) noexcept
{
	LogDispatchError(HttpStatus::BAD_GATEWAY,
			 "Configuration server failed",
			 ep, 1);
}

bool
Request::DoContentTypeLookup(const ResourceAddress &address) noexcept
{
	return suffix_registry_lookup(pool,
				      GetTranslationService(),
				      address,
				      stopwatch,
				      *this, cancel_ptr);
}

void
Request::HandleTranslatedRequest(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	translate.response = std::move(_response);
	const auto &response = *translate.response;

	/* copy the ResourceAddress from the TranslateResponse and
	   complete it with data which wasn't passed to the
	   translation server (e.g. the query string) */
	auto &address = translate.address;
	address = {ShallowCopy(), response.address};
	if (address.IsDefined()) {
		if (response.transparent &&
		    (dissected_uri.args.data() != nullptr ||
		     !dissected_uri.path_info.empty()))
			address = address.WithArgs(pool,
						   dissected_uri.args,
						   dissected_uri.path_info);

		processor_focus =
			/* the IsProcessorEnabled() check was disabled
			   because the response may include a
			   X-CM4all-View header that enables the
			   processor; with this check, the request
			   body would be consumed already */
			//IsProcessorEnabled() &&
			args.Get("focus") != nullptr;

		if (!processor_focus)
			/* forward query string */
			address = address.WithQueryStringFrom(pool, request.uri);

		if (address.IsCgiAlike() &&
		    (address.GetCgi().request_uri_verbatim ||
		     address.GetCgi().script_name == nullptr) &&
		    address.GetCgi().uri == nullptr)
			/* pass the "real" request URI to the CGI (but
			   without the "args", unless the request is
			   "transparent") */
			address.GetCgi().uri = ForwardURI();

		resource_tag = translate.address_id = address.GetId(pool);
	}

	translate.transformations.clear();

	if (response.generator != nullptr) {
		auto &rl = *(BpRequestLogger *)request.logger;
		rl.generator = p_strdup(request.pool, response.generator);
	}

	ApplyFileEnotdir();

	if (!DoContentTypeLookup(response.address)) {
		translate.suffix_transformations.clear();
		HandleTranslatedRequest2(response);
	}
}

[[gnu::pure]]
static const char *
uri_without_query_string(AllocatorPtr alloc, const char *uri) noexcept
{
	assert(uri != nullptr);

	const char *qmark = strchr(uri, '?');
	if (qmark != nullptr)
		return alloc.DupZ(std::string_view{uri, qmark});

	return uri;
}

static void
fill_translate_request_local_address(TranslateRequest &t,
				     const IncomingHttpRequest &r) noexcept
{
	t.local_address = r.local_address;
}

static void
fill_translate_request_remote_host(TranslateRequest &t,
				   const char *remote_host_and_port) noexcept
{
	t.remote_host = remote_host_and_port;
}

static void
fill_translate_request_user_agent(TranslateRequest &t,
				  const StringMap &headers) noexcept
{
	t.user_agent = headers.Get(user_agent_header);
}

static void
fill_translate_request_language(TranslateRequest &t,
				const StringMap &headers) noexcept
{
	t.accept_language = headers.Get(accept_language_header);
}

static void
fill_translate_request_args(TranslateRequest &t,
			    AllocatorPtr alloc, const StringMap &args) noexcept
{
	t.args = args_format(alloc, &args,
			     nullptr, {}, nullptr, {},
			     "translate");
	if (t.args != nullptr && *t.args == 0)
		t.args = nullptr;
}

static void
fill_translate_request_query_string(TranslateRequest &t,
				    AllocatorPtr alloc,
				    const DissectedUri &uri) noexcept
{
	t.query_string = uri.query.empty()
		? nullptr
		: alloc.DupZ(uri.query);
}

static void
fill_translate_request_user(Request &request,
			    TranslateRequest &t,
			    AllocatorPtr alloc) noexcept
{
	auto session = request.GetRealmSession();
	if (session) {
		if (session->user != nullptr)
			t.user = alloc.DupZ((std::string_view)session->user);
	}
}

[[gnu::pure]]
static const TranslationLayoutItem *
FindLayoutItem(std::span<const TranslationLayoutItem> items,
	       const char *uri) noexcept
{
	for (const auto &i : items)
		if (i.Match(uri))
			return &i;

	return nullptr;
}

inline void
Request::RepeatTranslation(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	const AllocatorPtr alloc(pool);

	const auto &response = *_response;

	if (response.layout.data() != nullptr) {
		/* repeat request with LAYOUT mirrored */
		assert(response.layout_items);

		if (++translate.n_layout > 4) {
			_response.reset();
			LogDispatchError(HttpStatus::BAD_GATEWAY,
					 "Too many consecutive LAYOUT packets",
					 1);
			return;
		}

		const char *uri = translate.request.uri;
		if (response.regex_tail && response.base != nullptr) {
			uri = StringAfterPrefix(uri, response.base);
			if (uri == nullptr) {
				_response.reset();
				LogDispatchError(HttpStatus::BAD_GATEWAY,
						 "Base mismatch", 1);
				return;
			}
		}

		translate.request.layout = response.layout;
		translate.layout_items = response.layout_items;
		translate.request.layout_item = FindLayoutItem(*response.layout_items,
							       uri);
	}

	bool save_previous = false;

	if (response.check.data() != nullptr) {
		/* repeat request with CHECK set */

		if (++translate.n_checks > 4) {
			_response.reset();
			LogDispatchError(HttpStatus::BAD_GATEWAY,
					 "Too many consecutive CHECK packets",
					 1);
			return;
		}

		save_previous = true;
		translate.request.check = response.check;

		if (response.check_header != nullptr) {
			const char *check_header =
				request.headers.Get(response.check_header);
			if (check_header == nullptr)
				check_header = "";
			translate.request.check_header =
				alloc.Concat(response.check_header, ":"sv,
					     check_header);
		} else
			translate.request.check_header = nullptr;

		translate.request.authorization = request.headers.Get(authorization_header);
	}

	if (response.internal_redirect.data() != nullptr) {
		/* repeat request with INTERNAL_REDIRECT set */

		assert(response.want_full_uri.data() == nullptr);

		if (++translate.n_internal_redirects > 4) {
			_response.reset();
			LogDispatchError(HttpStatus::BAD_GATEWAY,
					 "Too many consecutive INTERNAL_REDIRECT packets",
					 1);
			return;
		}

		save_previous = true;
		translate.request.internal_redirect = response.internal_redirect;

		/* reset "layout" because we're now serving a
		   different request */
		translate.request.layout = {};
		translate.request.layout_item = nullptr;

		assert(response.uri != nullptr);
		translate.request.uri = response.uri;

		translate.had_internal_redirect = true;

		dissected_uri.base = translate.request.uri;
	}

	if (response.like_host != nullptr) {
		/* repeat request with the given HOST */

		if (++translate.n_like_host > 4) {
			_response.reset();
			LogDispatchError(HttpStatus::BAD_GATEWAY,
					 "Too many consecutive LIKE_HOST packets",
					 1);
			return;
		}

		translate.request.host = response.like_host;
	}

	/* handle WANT */

	if (response.want.data() != nullptr)
		translate.request.want = response.want;

	if (response.Wants(TranslationCommand::LISTENER_TAG)) {
		_response.reset();
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Translation protocol 2 doesn't allow WANT/LISTENER_TAG",
				 1);
		return;
	}

	if (response.Wants(TranslationCommand::LOCAL_ADDRESS))
		fill_translate_request_local_address(translate.request, request);

	if (response.Wants(TranslationCommand::REMOTE_HOST))
		fill_translate_request_remote_host(translate.request,
						   connection.remote_host_and_port);

	if (response.Wants(TranslationCommand::USER_AGENT))
		fill_translate_request_user_agent(translate.request,
						  request.headers);

	if (response.Wants(TranslationCommand::LANGUAGE))
		fill_translate_request_language(translate.request,
						request.headers);

	if (response.Wants(TranslationCommand::ARGS) &&
	    translate.request.args == nullptr)
		fill_translate_request_args(translate.request, alloc, args);

	if (response.Wants(TranslationCommand::QUERY_STRING))
		fill_translate_request_query_string(translate.request,
						    alloc,
						    dissected_uri);

	if (response.Wants(TranslationCommand::QUERY_STRING))
		fill_translate_request_query_string(translate.request,
						    alloc,
						    dissected_uri);

	if (response.Wants(TranslationCommand::USER) || translate.want_user) {
		ApplyTranslateRealm(response, {});

		translate.want_user = true;
		fill_translate_request_user(*this, translate.request, alloc);
	}

	if (response.want_full_uri.data() != nullptr) {
		/* repeat request with full URI */

		/* echo the server's WANT_FULL_URI packet */
		translate.request.want_full_uri = response.want_full_uri;

		/* send the full URI this time */
		translate.request.uri = uri_without_query_string(alloc, request.uri);

		/* undo the uri_parse() call (but leave the query_string) */

		dissected_uri.base = translate.request.uri;
		dissected_uri.args = {};
		dissected_uri.path_info = {};
	}

	/* resend the modified request */

	if (save_previous)
		translate.previous = std::move(_response);
	else
		_response.reset();

	SubmitTranslateRequest();
}

inline void
Request::HandleChainResponse(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	assert(pending_chain_response);

	const auto &response = *_response;

	if (response.break_chain) {
		_response.reset();
		DispatchResponse(std::move(pending_chain_response));
		return;
	}

	if (response.internal_redirect.data() != nullptr) {
		pending_chain_response.reset();
		RepeatTranslation(std::move(_response));
		return;
	}

	if (CheckHandleRedirectBounceStatus(response))
		/* done */
		return;

	if (!response.address.IsDefined()) {
		_response.reset();
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Empty CHAIN response", 1);
		return;
	}

	if (!response.views.empty())
		translate.transformations = {ShallowCopy{}, response.views.front().transformations};
	else
		translate.transformations.clear();

	translate.chain = response.chain;
	if (translate.chain.data() != nullptr && ++translate.n_chain > 4) {
		_response.reset();
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Too many consecutive CHAIN packets", 1);
		return;
	}

	/* no caching for chained requests */
	auto &rl = *instance.direct_resource_loader;

	auto pr = std::move(*pending_chain_response);
	pending_chain_response.reset();

	HttpMethod method = HttpMethod::POST;
	if (translate.response->transparent_chain) {
		/* transparent chain mode: send the original request
		   method/body */
		method = request.method;
		pr.body = std::move(request_body);
	}

	/* promote the CHAIN response to the final response, so
	   its filter_4xx settings etc. are used */
	translate.response = std::move(_response);

	rl.SendRequest(pool, stopwatch,
		       {
			       .sticky_hash = session_id.GetClusterHash(),
			       .status = pr.status,
			       .want_metrics = translate.enable_metrics,
		       },
		       method, response.address,
		       std::move(pr.headers).ToMap(pool),
		       std::move(pr.body),
		       *this, cancel_ptr);

}

void
Request::OnTranslateResponse(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	const auto &response = *_response;

	if (response.protocol_version < 2) {
		_response.reset();
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Unsupported configuration server",
				 "Unsupported translation protocol version",
				 1);
		return;
	}

	if (response.listener_tag != nullptr)
		translate.request.listener_tag = response.listener_tag;

	if (response.defer) {
		_response.reset();
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Unexpected DEFER", 1);
		return;
	}

	if (pending_chain_response) {
		/* this is the response for a CHAIN request */
		HandleChainResponse(std::move(_response));
		return;
	}

	if (!response.allow_remote_networks.empty()) {
		if (const auto remote_address = GetRemoteAdress();
		    !response.allow_remote_networks.Contains(remote_address)) {
			_response.reset();
			DispatchError(HttpStatus::FORBIDDEN);
			return;
		}
	}

	if (response.https_only != 0 && !IsHttps()) {
		/* not encrypted: redirect to https:// */

		const char *host = request.headers.Get(host_header);
		if (host == nullptr) {
			_response.reset();
			DispatchError(HttpStatus::BAD_REQUEST, "No Host header");
			return;
		}

		std::string_view redirect = MakeHttpsRedirect(pool, host, response.https_only, request.uri);
		_response.reset();
		DispatchRedirect(HttpStatus::MOVED_PERMANENTLY,
				 redirect,
				 "This page requires \"https\"");
		return;
	}

	if (response.transparent) {
		MakeStateless();
		args.Clear();
	} else if (response.discard_session)
		DiscardSession();
	else if (response.discard_realm_session)
		DiscardRealmSession();

	if (response.session.data() != nullptr)
		/* must apply SESSION early so it gets used by
		   RepeatTranslation() */
		translate.request.session = response.session;

	if (response.realm_session.data() != nullptr)
		/* must apply REALM_SESSION early so it gets used by
		   RepeatTranslation() */
		translate.request.realm_session = response.realm_session;

	if (response.session_cookie_same_site != CookieSameSite::DEFAULT)
		session_cookie_same_site = response.session_cookie_same_site;

	translation_protocol_version_received = true;
	if (response.protocol_version > translation_protocol_version)
		translation_protocol_version = response.protocol_version;

	if (response.HasAuth())
		HandleAuth(std::move(_response));
	else if (response.http_auth.data() != nullptr &&
		 /* allow combining HTTP_AUTH and TOKEN_AUTH; in that
		    case, use HTTP_AUTH only if an "Authorization"
		    header was received */
		 (request.headers.Contains(authorization_header) ||
		  response.token_auth.data() == nullptr))
		HandleHttpAuth(std::move(_response));
	else if (response.token_auth.data() != nullptr)
		HandleTokenAuth(std::move(_response));
	else
		OnTranslateResponseAfterAuth(std::move(_response));
}

void
Request::OnTranslateResponseAfterAuth(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	const auto &response = *_response;

	if (response.check.data() != nullptr ||
	    response.layout.data() != nullptr ||
	    response.internal_redirect.data() != nullptr||
	    response.like_host != nullptr ||
	    !response.want.empty() ||
	    /* after successful new authentication, repeat the translation
	       if the translation server wishes to know the user */
	    (translate.want_user && translate.user_modified) ||
	    response.want_full_uri.data() != nullptr) {

		/* repeat translation due to want_user||user_modified only
		   once */
		translate.user_modified = false;

		RepeatTranslation(std::move(_response));
		return;
	}

	/* the CHECK is done by now; don't carry the CHECK value on to
	   further translation requests */
	translate.request.check = {};
	/* also reset the counter so we don't trigger the endless
	   recursion detection by the ENOTDIR chain */
	translate.n_checks = 0;
	translate.n_internal_redirects = 0;

	if (response.previous) {
		if (!translate.previous) {
			_response.reset();
			LogDispatchError(HttpStatus::BAD_GATEWAY,
					 "No previous translation response", 1);
			return;
		}

		/* apply changes from this response, then resume the
		   "previous" response */
		ApplyTranslateResponseSession(response);

		_response = std::move(translate.previous);
	}

	OnTranslateResponse2(std::move(_response));
}

inline void
Request::OnTranslateResponse2(UniquePoolPtr<TranslateResponse> &&_response) noexcept
{
	const auto &response = *_response;

	if (CheckHandleReadFile(response))
		return;

	if (CheckHandlePathExists(response))
		return;

	if (CheckHandleProbePathSuffixes(response))
		return;

	/* check ENOTDIR */
	if (response.enotdir.data() != nullptr) {
		CheckFileEnotdir(std::move(_response));
		return;
	}

	OnTranslateResponseAfterEnotdir(std::move(_response));
}

void
Request::OnTranslateResponseAfterEnotdir(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	assert(_response);

	const auto &response = *_response;

	/* check if the file exists */
	if (response.file_not_found.data() != nullptr) {
		CheckFileNotFound(std::move(_response));
		return;
	}

	OnTranslateResponseAfterFileNotFound(std::move(_response));
}

void
Request::OnTranslateResponseAfterFileNotFound(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	assert(_response);

	const auto &response = *_response;

	/* check if it's a directory */
	if (response.directory_index.data() != nullptr) {
		CheckDirectoryIndex(std::move(_response));
		return;
	}

	OnTranslateResponseAfterDirectoryIndex(std::move(_response));
}

void
Request::OnTranslateResponseAfterDirectoryIndex(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	HandleTranslatedRequest(std::move(_response));
}

inline bool
Request::CheckHandleReadFile(const TranslateResponse &response) noexcept
{
	if (response.read_file == nullptr)
		return false;

	if (++translate.n_read_file > 2) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Too many consecutive READ_FILE packets", 1);
		return true;
	}

	std::tie(translate.request.read_file, translate.read_file_lease) = instance.file_cache.Get(response.read_file, 256);

	if (translate.request.read_file.data() == nullptr)
		/* special case: if the file does not exist, return an empty
		   READ_FILE packet to the translation server */
		translate.request.read_file = AsBytes(""sv);

	SubmitTranslateRequest();
	return true;
}

inline bool
Request::CheckHandlePathExists(const TranslateResponse &response) noexcept
{
	if (!response.path_exists)
		return false;

	if (++translate.n_path_exists > 2) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Too many consecutive PATH_EXISTS packets",
				 1);
		return true;
	}

	// TODO use io_uring

	if (response.address.type != ResourceAddress::Type::LOCAL) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "PATH_EXISTS without PATH", 1);
		return true;
	}

	HandlePathExists(response.address.GetFile());
	return true;
}

void
Request::OnTranslateError(std::exception_ptr ep) noexcept
{
	LogDispatchError(HttpStatus::BAD_GATEWAY,
			 "Configuration server failed", ep, 1);
}

void
Request::SubmitTranslateRequest() noexcept
{
	GetTranslationService().SendRequest(pool,
					    translate.request,
					    stopwatch,
					    *this,
					    cancel_ptr);
}

inline bool
Request::ParseRequestUri() noexcept
{
	if (!uri_path_verify_quick(request.uri) ||
	    !dissected_uri.Parse(request.uri)) {
		DispatchError(HttpStatus::BAD_REQUEST, "Malformed URI");
		return false;
	}

	return true;
}

static void
fill_translate_request(TranslateRequest &t,
		       const IncomingHttpRequest &request,
		       const DissectedUri &uri,
		       const char *listener_tag)
{
	const AllocatorPtr alloc(request.pool);

	t.host = request.headers.Get(host_header);
	t.uri = alloc.DupZ(uri.base);

	t.listener_tag = listener_tag;
}

void
Request::HandleHttpRequest(CancellablePointer &caller_cancel_ptr) noexcept
{
	caller_cancel_ptr = *this;

	if (!ParseRequestUri())
		return;

	assert(!dissected_uri.base.empty());
	assert(dissected_uri.base.front() == '/');

	ParseArgs();
	DetermineSession();

	fill_translate_request(translate.request,
			       request,
			       dissected_uri,
			       connection.listener.GetTag());

	if (translate.request.host == nullptr) {
		DispatchError(HttpStatus::BAD_REQUEST, "No Host header");
		return;
	} else if (!VerifyUriHostPort(translate.request.host)) {
		DispatchError(HttpStatus::BAD_REQUEST, "Malformed Host header");
		return;
	}

	SubmitTranslateRequest();
}
