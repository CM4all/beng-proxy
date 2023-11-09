// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "translation/Handler.hxx"
#include "translation/Response.hxx"
#include "co/AwaitableHelper.hxx"
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

	using Awaitable = Co::AwaitableHelper<CoTranslate>;
	friend Awaitable;

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

	Awaitable operator co_await() noexcept {
		return *this;
	}

private:
	bool IsReady() const noexcept {
		return !cancel_ptr;
	}

	UniquePoolPtr<TranslateResponse> TakeValue() noexcept {
		return std::move(response);
	}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> _response) noexcept override;
	void OnTranslateError(std::exception_ptr _error) noexcept override;
};
