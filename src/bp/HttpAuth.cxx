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
 * #TranslationCommand::HTTP_AUTH implementation.
 */

#include "Request.hxx"
#include "Connection.hxx"
#include "Instance.hxx"
#include "pool/pool.hxx"
#include "translation/Handler.hxx"
#include "translation/Service.hxx"

inline void
Request::OnHttpAuthTranslateResponse(const TranslateResponse &response) noexcept
{
	if (CheckHandleRedirectBounceStatus(response))
		return;

	user = response.user;
	if (user == nullptr) {
		DispatchError(HTTP_STATUS_UNAUTHORIZED, "Unauthorized");
		return;
	}

	OnTranslateResponseAfterAuth(*translate.previous);
}

inline void
Request::OnHttpAuthTranslateError(std::exception_ptr ep) noexcept
{
	LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
			 "Configuration server failed", ep, 1);
}

class HttpAuthTranslateHandler final : public TranslateHandler {
	Request &request;

public:
	explicit HttpAuthTranslateHandler(Request &_request) noexcept
		:request(_request) {}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(TranslateResponse &response) noexcept override {
		request.OnHttpAuthTranslateResponse(response);
	}

	void OnTranslateError(std::exception_ptr error) noexcept override {
		request.OnHttpAuthTranslateError(std::move(error));
	}
};

void
Request::HandleHttpAuth(const TranslateResponse &response) noexcept
{
	assert(!response.http_auth.IsNull());

	const char *authorization = translate.request.authorization;

	if (authorization == nullptr) {
		DispatchError(HTTP_STATUS_UNAUTHORIZED, "Unauthorized");
		return;
	}

	auto t = NewFromPool<TranslateRequest>(pool);
	t->http_auth = response.http_auth;
	t->authorization = authorization;

	translate.previous = &response;

	auto *http_auth_translate_handler =
		NewFromPool<HttpAuthTranslateHandler>(pool, *this);

	GetTranslationService().SendRequest(pool, *t,
					    stopwatch,
					    *http_auth_translate_handler,
					    cancel_ptr);
}

