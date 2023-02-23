// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * #TranslationCommand::HTTP_AUTH implementation.
 */

#include "Request.hxx"
#include "Connection.hxx"
#include "Instance.hxx"
#include "pool/pool.hxx"
#include "http/IncomingRequest.hxx"
#include "translation/Handler.hxx"
#include "translation/Service.hxx"
#include "AllocatorPtr.hxx"

inline void
Request::OnHttpAuthTranslateResponse(const TranslateResponse &response) noexcept
{
	if (CheckHandleRedirectBounceStatus(response))
		return;

	user = response.user;
	if (user == nullptr) {
		DispatchError(HttpStatus::UNAUTHORIZED, "Unauthorized");
		return;
	}

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
		request.OnHttpAuthTranslateResponse(*response);
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

	const char *authorization = request.headers.Get("authorization");

	if (authorization == nullptr) {
		DispatchError(HttpStatus::UNAUTHORIZED, "Unauthorized");
		return;
	}

	auto t = NewFromPool<TranslateRequest>(pool);
	t->http_auth = response.http_auth;
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

