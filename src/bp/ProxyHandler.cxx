// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Serve HTTP requests from another HTTP server.
 */

#include "Request.hxx"
#include "Instance.hxx"
#include "ForwardRequest.hxx"
#include "CsrfProtection.hxx"
#include "http/IncomingRequest.hxx"
#include "http/rl/ResourceLoader.hxx"
#include "istream/AutoPipeIstream.hxx"
#include "uri/Recompose.hxx"
#include "AllocatorPtr.hxx"

/**
 * Return a copy of the URI for forwarding to the next server.  This
 * omits the beng-proxy request "arguments".
 */
[[gnu::pure]]
static const char *
ForwardURI(AllocatorPtr alloc, DissectedUri uri) noexcept
{
	uri.args = {};
	return RecomposeUri(alloc, uri);
}

const char *
Request::ForwardURI() const noexcept
{
	const TranslateResponse &t = *translate.response;
	if (t.transparent || dissected_uri.args.data() == nullptr) {
		/* transparent or no args: return the full URI as-is */

		if (translate.had_internal_redirect)
			/* after an internal redirect, we need to use
			   the new URI (and add query string / args);
			   dissected_uri.base has already been
			   updated, but request.uri is still the
			   original request URI */
			return RecomposeUri(pool, dissected_uri);

		return request.uri;
	} else
		/* remove the "args" part */
		return ::ForwardURI(pool, dissected_uri);
}

void
Request::HandleProxyAddress() noexcept
{
	const TranslateResponse &tr = *translate.response;
	const ResourceAddress &address = translate.address;

	assert(address.type == ResourceAddress::Type::HTTP ||
	       address.type == ResourceAddress::Type::LHTTP ||
	       address.IsCgiAlike());

	cookie_uri = address.GetUriPath();

	auto forward = ForwardRequest(tr.request_header_forward,
				      address.IsAnyHttp());

	if (tr.require_csrf_token &&
	    MethodNeedsCsrfProtection(forward.method) &&
	    !CheckCsrfToken())
		return;

	if (forward.body)
		forward.body = NewAutoPipeIstream(&pool, std::move(forward.body),
						  instance.pipe_stock);

	for (const auto &i : tr.request_headers)
		forward.headers.SecureSet(pool, i.key, i.value);

	collect_cookies = tr.response_header_forward.IsCookieMangle();

	auto &rl = tr.uncached
		? *instance.direct_resource_loader
		: *instance.cached_resource_loader;

	rl.SendRequest(pool, stopwatch,
		       {
			       session_id.GetClusterHash(),
			       tr.ignore_no_cache,
			       tr.eager_cache,
			       tr.auto_flush_cache,
			       translate.enable_metrics,
			       tr.cache_tag,
			       tr.site,
		       },
		       forward.method, address, HttpStatus::OK,
		       std::move(forward.headers),
		       std::move(forward.body),
		       nullptr,
		       *this, cancel_ptr);
}
