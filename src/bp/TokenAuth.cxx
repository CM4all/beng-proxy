/*
 * Copyright 2007-2021 CM4all GmbH
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
 * #TranslationCommand::TOKEN_AUTH implementation.
 */

#include "Request.hxx"
#include "Connection.hxx"
#include "Instance.hxx"
#include "puri_escape.hxx"
#include "http/IncomingRequest.hxx"
#include "translation/Handler.hxx"
#include "translation/Service.hxx"
#include "pool/pool.hxx"
#include "uri/Recompose.hxx"
#include "util/IterableSplitString.hxx"

#include <stdexcept>

static const char *
GetTokenAuthRedirectUri(AllocatorPtr alloc,
			const char *scheme, const char *host,
			DissectedUri dissected_uri,
			const TranslateResponse &response) noexcept
{
	/* TODO: deduplicate code from GetBounceUri() */

	if (response.uri != nullptr) {
		dissected_uri.base = response.uri;
		dissected_uri.path_info = nullptr;
	}

	const char *uri_path = RecomposeUri(alloc, dissected_uri);

	return alloc.Concat(scheme, "://", host, uri_path);
}

inline void
Request::OnTokenAuthTranslateResponse(const TranslateResponse &response) noexcept
{
	if (response.discard_session)
		DiscardSession();

	bool is_authenticated = false;
	{
		auto session = ApplyTranslateSession(response);
		if (session)
			is_authenticated = session->user != nullptr;
	}

	if (CheckHandleRedirectBounceStatus(response))
		return;

	if (!is_authenticated) {
		/* for some reason, the translation server did not send
		   REDIRECT/BOUNCE/STATUS, but we still don't have a user -
		   this should not happen; bail out, don't dare to accept the
		   client */
		DispatchError(HTTP_STATUS_FORBIDDEN, "Forbidden");
		return;
	}

	translate.user_modified = response.user != nullptr;

	/* using the original translation response, because it may
	   have information about the original request */
	const auto &tr = *translate.response;

	/* don't call OnTranslateResponseAfterAuth() here, instead
	   redirect to the URI with auth_token removed */

	const char *redirect_uri =
		GetTokenAuthRedirectUri(pool,
					GetExternalUriScheme(tr),
					GetExternalUriHost(tr),
					dissected_uri,
					*translate.response);

	DispatchRedirect(HTTP_STATUS_SEE_OTHER, redirect_uri, nullptr);
}

inline void
Request::OnTokenAuthTranslateError(std::exception_ptr ep) noexcept
{
	LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
			 "Configuration server failed", ep, 1);
}

class TokenAuthTranslateHandler final : public TranslateHandler {
	Request &request;

public:
	explicit TokenAuthTranslateHandler(Request &_request) noexcept
		:request(_request) {}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(TranslateResponse &response) noexcept override {
		request.OnTokenAuthTranslateResponse(response);
	}

	void OnTranslateError(std::exception_ptr error) noexcept override {
		request.OnTokenAuthTranslateError(std::move(error));
	}
};

static StringView
ConcatQueryStrings(AllocatorPtr alloc, StringView a, StringView b) noexcept
{
	/* strip redundant ampersands */
	if (a.EndsWith('&') && (b.empty() || b.StartsWith('&')))
		a.pop_back();

	if (a.empty() && b.StartsWith('&'))
		b.pop_front();

	/* shortcut: if both are empty, the query string is gone
	   completely */
	if (a.empty() && b.empty())
		return nullptr;

	/* concatenate both parts */
	return alloc.Concat(a, b);
}

static StringView
RemoveFromQueryString(AllocatorPtr alloc, StringView q,
		      StringView name, StringView value) noexcept
{
	StringView a(q.begin(), name.begin());
	StringView b(value.end(), q.end());

	return ConcatQueryStrings(alloc, a, b);
}

static const char *
ExtractAuthToken(AllocatorPtr alloc, DissectedUri &dissected_uri)
{
	char *auth_token = nullptr;

	for (const auto i : IterableSplitString(dissected_uri.query, '&')) {
		const auto [name, escaped_value] = i.Split('=');
		if (!StringView("access_token").Equals(name))
			continue;

		auth_token = uri_unescape_dup(alloc, escaped_value);
		if (auth_token == nullptr)
			throw std::invalid_argument("Malformed auth token");

		/* remove the "access_token" parameter from the query
		   string */
		dissected_uri.query =
			RemoveFromQueryString(alloc, dissected_uri.query,
					      name, escaped_value);

		break;
	}

	return auth_token;
}

void
Request::HandleTokenAuth(const TranslateResponse &response) noexcept
{
	assert(!response.token_auth.IsNull());

	/* we need to validate the session realm early */
	ApplyTranslateRealm(response, nullptr);

	const char *auth_token;

	try {
		auth_token = ExtractAuthToken(pool, dissected_uri);
	} catch (const std::invalid_argument &e) {
		DispatchError(HTTP_STATUS_BAD_REQUEST, e.what());
		return;
	}

	if (auth_token == nullptr) {
		bool is_authenticated = false;
		{
			auto session = GetRealmSession();
			if (session)
				is_authenticated = session->user != nullptr;
		}

		if (is_authenticated) {
			/* already authenticated; we can skip the
			   TOKEN_AUTH request */
			OnTranslateResponseAfterAuth(response);
			return;
		}
	}

	auto t = NewFromPool<TranslateRequest>(pool);
	t->token_auth = response.token_auth;
	t->auth_token = auth_token;
	t->uri = auth_token != nullptr
		? RecomposeUri(pool, dissected_uri)
		: request.uri;
	t->host = translate.request.host;
	t->session = translate.request.session;

	translate.previous = &response;

	auto *http_auth_translate_handler =
		NewFromPool<TokenAuthTranslateHandler>(pool, *this);

	GetTranslationService().SendRequest(pool, *t,
					    stopwatch,
					    *http_auth_translate_handler,
					    cancel_ptr);
}
