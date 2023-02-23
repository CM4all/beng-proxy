// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
	UniquePoolPtr<TranslateResponse> response;
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

			UniquePoolPtr<TranslateResponse> await_resume() const {
				return request.AwaitResume();
			}
		};

		return Awaitable{*this};
	}

private:
	bool IsReady() const noexcept {
		return !cancel_ptr;
	}

	UniquePoolPtr<TranslateResponse> AwaitResume() {
		if (error)
			std::rethrow_exception(std::move(error));

		return std::move(response);
	}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> _response) noexcept override;
	void OnTranslateError(std::exception_ptr _error) noexcept override;
};
