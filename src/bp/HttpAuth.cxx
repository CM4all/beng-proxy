// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * #TranslationCommand::HTTP_AUTH implementation.
 */

#include "Request.hxx"
#include "Connection.hxx"
#include "Instance.hxx"
#include "pool/pool.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"
#include "translation/Handler.hxx"
#include "translation/Service.hxx"
#include "AllocatorPtr.hxx"

inline void
Request::OnHttpAuthTranslateResponse(UniquePoolPtr<TranslateResponse> &&_response) noexcept
{
	const auto &response = *_response;

	if (CheckHandleRedirectBounceStatus(response))
		return;

	user = response.user;
	if (user == nullptr) {
		_response.reset();
		DispatchError(HttpStatus::UNAUTHORIZED);
		return;
	}

	_response.reset();

	OnTranslateResponseAfterAuth(std::move(translate.previous));
}

inline void
Request::OnHttpAuthTranslateError(std::exception_ptr ep) noexcept
{
	LogDispatchError(HttpStatus::BAD_GATEWAY,
			 "Configuration server failed", ep, 1);
}

class HttpAuthTranslateHandler final : public TranslateHandler {
	Request &request;

public:
	explicit HttpAuthTranslateHandler(Request &_request) noexcept
		:request(_request) {}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept override {
		request.OnHttpAuthTranslateResponse(std::move(response));
	}

	void OnTranslateError(std::exception_ptr error) noexcept override {
		request.OnHttpAuthTranslateError(std::move(error));
	}
};

void
Request::HandleHttpAuth(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	const auto &response = *_response;

	assert(response.http_auth.data() != nullptr);

	const char *authorization = request.headers.Get(authorization_header);

	if (authorization == nullptr) {
		DispatchError(HttpStatus::UNAUTHORIZED);
		return;
	}

	auto http_auth = response.http_auth;
	if (!response.append_auth.empty()) {
		const AllocatorPtr alloc{pool};
		http_auth = alloc.LazyConcat(http_auth, response.append_auth);
	}

	auto t = NewFromPool<TranslateRequest>(pool);
	t->http_auth = http_auth;
	t->authorization = authorization;
	t->listener_tag = translate.request.listener_tag;
	t->host = translate.request.host;

	translate.previous = std::move(_response);

	auto *http_auth_translate_handler =
		NewFromPool<HttpAuthTranslateHandler>(pool, *this);

	GetTranslationService().SendRequest(pool, *t,
					    stopwatch,
					    *http_auth_translate_handler,
					    cancel_ptr);
}

