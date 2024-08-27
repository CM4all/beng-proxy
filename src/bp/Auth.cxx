// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * #TRANSLATE_AUTH implementation.
 */

#include "Request.hxx"
#include "Connection.hxx"
#include "Listener.hxx"
#include "Instance.hxx"
#include "session/Lease.hxx"
#include "session/Session.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"
#include "pool/pool.hxx"
#include "translation/Handler.hxx"
#include "translation/Service.hxx"
#include "load_file.hxx"
#include "AllocatorPtr.hxx"

inline void
Request::OnAuthTranslateResponse(UniquePoolPtr<TranslateResponse> &&_response) noexcept
{
	const auto &response = *_response;

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
		_response.reset();
		DispatchError(HttpStatus::FORBIDDEN, "Forbidden");
		return;
	}

	translate.user_modified = response.user != nullptr;
	_response.reset();

	OnTranslateResponseAfterAuth(std::move(translate.previous));
}

inline void
Request::OnAuthTranslateError(std::exception_ptr ep) noexcept
{
	LogDispatchError(HttpStatus::BAD_GATEWAY,
			 "Configuration server failed", ep, 1);
}

class AuthTranslateHandler final : public TranslateHandler {
	Request &request;

public:
	explicit AuthTranslateHandler(Request &_request) noexcept
		:request(_request) {}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept override {
		request.OnAuthTranslateResponse(std::move(response));
	}

	void OnTranslateError(std::exception_ptr error) noexcept override {
		request.OnAuthTranslateError(error);
	}
};

void
Request::HandleAuth(UniquePoolPtr<TranslateResponse> _response)
{
	const auto &response = *_response;

	assert(response.protocol_version >= 2);
	assert(response.HasAuth());

	auto auth = response.auth;
	if (auth.data() == nullptr) {
		/* load #TRANSLATE_AUTH_FILE */
		assert(response.auth_file != nullptr);

		try {
			auth = LoadFile(pool, response.auth_file, 64);
		} catch (...) {
			LogDispatchError(std::current_exception());
			return;
		}
	} else {
		assert(response.auth_file == nullptr);
	}

	const auto auth_base = auth;

	if (response.append_auth.data() != nullptr) {
		assert(auth.data() != nullptr);

		const AllocatorPtr alloc(pool);
		auth = alloc.LazyConcat(auth, response.append_auth);
	}

	/* we need to validate the session realm early */
	ApplyTranslateRealm(response, auth_base);

	bool is_authenticated = false;
	{
		auto session = GetRealmSession();
		if (session)
			is_authenticated = session->user != nullptr;
	}

	if (is_authenticated) {
		/* already authenticated; we can skip the AUTH request */
		OnTranslateResponseAfterAuth(std::move(_response));
		return;
	}

	auto t = NewFromPool<TranslateRequest>(pool);
	t->auth = auth;
	t->uri = request.uri;
	t->host = translate.request.host;
	t->session = translate.request.session;
	t->listener_tag = translate.request.listener_tag;

	if (connection.listener.GetAuthAltHost())
		t->alt_host = request.headers.Get(x_cm4all_althost_header);

	translate.previous = std::move(_response);

	auto *auth_translate_handler =
		NewFromPool<AuthTranslateHandler>(pool, *this);

	GetTranslationService().SendRequest(pool, *t,
					    stopwatch,
					    *auth_translate_handler,
					    cancel_ptr);
}
