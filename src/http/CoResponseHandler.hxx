// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "ResponseHandler.hxx"
#include "pool/UniquePtr.hxx"
#include "co/AwaitableHelper.hxx"
#include "util/Cancellable.hxx"

struct PendingResponse;

/**
 * Awaitable #HttpResponseHandler implementation, to be used in
 * coroutines.
 */
class CoHttpResponseHandler
	: protected HttpResponseHandler
{
	struct pool &pool;

	UniquePoolPtr<PendingResponse> response;
	std::exception_ptr error;

	std::coroutine_handle<> continuation;

	using Awaitable = Co::AwaitableHelper<CoHttpResponseHandler>;
	friend Awaitable;

protected:
	CancellablePointer cancel_ptr;

public:
	explicit CoHttpResponseHandler(struct pool &_pool) noexcept
		:pool(_pool) {}

	~CoHttpResponseHandler() noexcept {
		if (cancel_ptr)
			cancel_ptr.Cancel();
	}

	CoHttpResponseHandler(const CoHttpResponseHandler &) = delete;
	CoHttpResponseHandler &operator=(const CoHttpResponseHandler &) = delete;

	Awaitable operator co_await() noexcept {
		return *this;
	}

private:
	bool IsReady() const noexcept {
		return !cancel_ptr;
	}

	UniquePoolPtr<PendingResponse> TakeValue() noexcept {
		return std::move(response);
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept final;
	void OnHttpError(std::exception_ptr error) noexcept final;
};
