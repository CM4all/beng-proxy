/*
 * Copyright 2007-2019 Content Management AG
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
 * Serve HTTP requests from another HTTP server.
 */

#include "Handler.hxx"
#include "Connection.hxx"
#include "Instance.hxx"
#include "Request.hxx"
#include "ForwardRequest.hxx"
#include "CsrfProtection.hxx"
#include "ResourceLoader.hxx"
#include "HttpResponseHandler.hxx"
#include "http/IncomingRequest.hxx"
#include "http_address.hxx"
#include "cgi/Address.hxx"
#include "uri/Extract.hxx"
#include "istream/AutoPipeIstream.hxx"
#include "lhttp_address.hxx"
#include "pool/pool.hxx"
#include "AllocatorPtr.hxx"

/**
 * Return a copy of the URI for forwarding to the next server.  This
 * omits the beng-proxy request "arguments".
 */
gcc_pure
static const char *
ForwardURI(AllocatorPtr alloc, const DissectedUri &uri) noexcept
{
	if (uri.query.empty())
		return alloc.DupZ(uri.base);
	else
		return alloc.Concat(uri.base, '?', uri.query);
}

inline const char *
Request::ForwardURI() const noexcept
{
	const TranslateResponse &t = *translate.response;
	if (t.transparent || dissected_uri.args == nullptr)
		/* transparent or no args: return the full URI as-is */
		return request.uri;
	else
		/* remove the "args" part */
		return ::ForwardURI(pool, dissected_uri);
}

void
Request::HandleProxyAddress() noexcept
{
	const TranslateResponse &tr = *translate.response;
	ResourceAddress address(ShallowCopy(), translate.address);

	assert(address.type == ResourceAddress::Type::HTTP ||
	       address.type == ResourceAddress::Type::LHTTP ||
	       address.type == ResourceAddress::Type::NFS ||
	       address.IsCgiAlike());

	if (translate.response->transparent &&
	    (!dissected_uri.args.IsNull() || !dissected_uri.path_info.empty()))
		address = address.WithArgs(pool,
					   dissected_uri.args,
					   dissected_uri.path_info);

	if (!processor_focus)
		/* forward query string */
		address = address.WithQueryStringFrom(pool, request.uri);

	if (address.IsCgiAlike() &&
	    address.GetCgi().script_name == nullptr &&
	    address.GetCgi().uri == nullptr)
		/* pass the "real" request URI to the CGI (but without the
		   "args", unless the request is "transparent") */
		address.GetCgi().uri = ForwardURI();

	cookie_uri = address.GetUriPath();

	auto forward = request_forward(*this,
				       tr.request_header_forward,
				       GetCookieHost(),
				       GetCookieURI(),
				       address.IsAnyHttp());

	if (tr.require_csrf_token &&
	    MethodNeedsCsrfProtection(forward.method) &&
	    !CheckCsrfToken())
		return;

#ifdef SPLICE
	if (forward.body)
		forward.body = NewAutoPipeIstream(&pool, std::move(forward.body),
						  instance.pipe_stock);
#endif

	for (const auto &i : tr.request_headers)
		forward.headers.SecureSet(pool, i.key, i.value);

	collect_cookies = true;

	auto &rl = tr.uncached
		? *instance.direct_resource_loader
		: *instance.cached_resource_loader;

	rl.SendRequest(pool, stopwatch,
		       session_id.GetClusterHash(),
		       nullptr, tr.site,
		       forward.method, address, HTTP_STATUS_OK,
		       std::move(forward.headers),
		       std::move(forward.body),
		       nullptr,
		       *this, cancel_ptr);
}
