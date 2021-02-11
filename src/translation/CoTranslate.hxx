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

#pragma once

#include "translation/Handler.hxx"
#include "translation/Response.hxx"
#include "co/Compat.hxx"
#include "util/Cancellable.hxx"

struct TranslateRequest;
class AllocatorPtr;
class TranslationService;
class StopwatchPtr;

class CoTranslate final : TranslateHandler {
	TranslateResponse *response;
	std::exception_ptr error;

	CancellablePointer cancel_ptr;

	std::coroutine_handle<> continuation;

public:
	CoTranslate(TranslationService &service,
		    AllocatorPtr alloc,
		    const TranslateRequest &request,
		    const StopwatchPtr &parent_stopwatch) noexcept;

	~CoTranslate() noexcept {
		if (cancel_ptr)
			cancel_ptr.Cancel();
	}

	CoTranslate(const CoTranslate &) = delete;
	CoTranslate &operator=(const CoTranslate &) = delete;

	auto operator co_await() noexcept {
		struct Awaitable final {
			CoTranslate &request;

			bool await_ready() const noexcept {
				return request.IsReady();
			}

			std::coroutine_handle<> await_suspend(std::coroutine_handle<> _continuation) const noexcept {
				request.continuation = _continuation;
				return std::noop_coroutine();
			}

			const TranslateResponse &await_resume() const {
				return request.AwaitResume();
			}
		};

		return Awaitable{*this};
	}

private:
	bool IsReady() const noexcept {
		return !cancel_ptr;
	}

	const TranslateResponse &AwaitResume() const {
		if (error)
			std::rethrow_exception(std::move(error));

		return *response;
	}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(TranslateResponse &_response) noexcept override;
	void OnTranslateError(std::exception_ptr _error) noexcept override;
};
